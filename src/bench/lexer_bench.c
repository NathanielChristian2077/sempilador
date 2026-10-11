
#define _POSIX_C_SOURCE 200809L

#include "../lexer/lexer.h"
#include "../source/source.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define SAMPLES 31
#define MIN_SAMPLE_SECONDS 0.05
#define MAX_REPEATS (1u << 20)

typedef struct {
    uint64_t tokens;
    uint64_t checksum;
} ScanResult;

static volatile uint64_t benchmark_sink = 0;

static double now_seconds(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("clock_gettime");
        exit(EXIT_FAILURE);
    }

    return (double)ts.tv_sec +
           (double)ts.tv_nsec * 1e-9;
}

static ScanResult scan_source(const SourceBuffer *src)
{
    Lexer lexer;
    lexer_init(&lexer, src->data, src->length);

    ScanResult result = {0};

    for (;;) {
        Token token = lexer_next(&lexer);

        if (token.kind == TOK_EOF)
            break;

        uint64_t offset =
            (uint64_t)(token.begin - src->data);

        result.tokens++;

        result.checksum =
            result.checksum * UINT64_C(33)
            + (uint64_t)token.kind
            + (uint64_t)token.length
            + offset
            + (uint64_t)token.line
            + (uint64_t)token.column;
    }

    return result;
}

static void run_batch(
    const SourceBuffer *src,
    size_t repetitions
) {
    uint64_t accumulator = 0;

    for (size_t i = 0; i < repetitions; ++i) {
        ScanResult result = scan_source(src);

        accumulator +=
            result.checksum ^ (uint64_t)i;
    }

    benchmark_sink ^= accumulator;
}

static int compare_double(
    const void *a,
    const void *b
) {
    double x = *(const double *)a;
    double y = *(const double *)b;

    return (x > y) - (x < y);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr,
            "Uso: %s <arquivo>\n", argv[0]);
        return EXIT_FAILURE;
    }

    SourceBuffer source;

    if (source_load(argv[1], &source) != SOURCE_OK) {
        fprintf(stderr,
            "Erro ao carregar: %s\n", argv[1]);
        return EXIT_FAILURE;
    }

    ScanResult reference = scan_source(&source);

    // Aquecimento inicial
    for (int i = 0; i < 3; ++i)
        run_batch(&source, 1);

    // Calibração automática
    size_t repetitions = 1;

    for (;;) {
        double start = now_seconds();

        run_batch(&source, repetitions);

        double elapsed = now_seconds() - start;

        if (elapsed >= MIN_SAMPLE_SECONDS ||
            repetitions >= MAX_REPEATS)
            break;

        repetitions *= 2;
    }

    // Coleta das amostras
    double samples[SAMPLES];

    for (size_t i = 0; i < SAMPLES; ++i) {
        double start = now_seconds();

        run_batch(&source, repetitions);

        double elapsed = now_seconds() - start;

        samples[i] = elapsed / (double)repetitions;
    }

    qsort(samples, SAMPLES,
          sizeof(samples[0]), compare_double);

    double median = samples[SAMPLES / 2];
    double p95 = samples[29];

    double mib = (double)source.length / 1048576.0;

    double throughput = median > 0
        ? mib / median
        : 0;

    double ns_per_token = reference.tokens > 0
        ? median * 1e9 / (double)reference.tokens
        : 0;

    printf(
        "%s,%zu,%" PRIu64 ",%zu,"
        "%.6f,%.6f,%.3f,%.3f,%" PRIu64 "\n",
        argv[1],
        source.length,
        reference.tokens,
        repetitions,
        median * 1000.0,
        p95 * 1000.0,
        throughput,
        ns_per_token,
        reference.checksum
    );

    source_free(&source);

    return EXIT_SUCCESS;
}
