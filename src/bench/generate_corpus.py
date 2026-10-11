from pathlib import Path
import random

OUT = Path("bench/corpus")

SIZES = {
    "64k": 64 * 1024,
    "1m": 1024 * 1024,
    "8m": 8 * 1024 * 1024,
}

BANKS = {
    "keywords": [
        "if ", "int ", "for ", "char ", "else ",
        "void ", "main ", "func ", "float ",
        "while ", "scanf ", "return ",
        "boolean ", "println ",
    ],
    "identifiers": [
        "x ", "counter ", "my_variable ",
        "accumulator ", "temporary_result ",
        "vector_index ", "current_iteration ",
        "some_very_long_identifier_name ",
    ],
    "prefixes": [
        "integer ", "ifelse ", "booleanValue ",
        "printlnBuffer ", "returnCode ",
        "whileLoop ", "scanfBuffer ",
        "floating ", "function ", "character ",
    ],
    "operators": [
        "x = 123; ", "y = 12.5; ",
        "x >= 10; ", "x != 0; ",
        "a && b; ", "a || !b; ",
        "x + y * 2; ", "x <= y; ",
        "x % 3; ",
    ],
    "mixed": [
        "int main() {\n",
        "int counter = 0;\n",
        "float value = 12.5;\n",
        "counter = counter + 1;\n",
        "if (counter >= 10) {\n",
        'println("ready");\n',
        "}\n",
        "while (counter < 100) {\n",
        "counter = counter + 1;\n",
        "}\n",
        "// example comment\n",
        "}\n",
    ],
}

def generate(path: Path, bank, size: int, seed: int):
    rng = random.Random(seed)
    entries = [item.encode("ascii") for item in bank]

    remaining = size

    with path.open("wb") as file:
        while remaining > 0:
            entry = entries[rng.randrange(len(entries))]

            if len(entry) > remaining:
                file.write(b" " * remaining)
                break

            file.write(entry)
            remaining -= len(entry)


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    for group_index, (name, bank) in enumerate(BANKS.items()):
        for size_index, (suffix, size) in enumerate(SIZES.items()):
            path = OUT / f"{name}_{suffix}.txt"

            seed = (
                0xC0FFEE
                + 100 * group_index
                + size_index
            )

            generate(path, bank, size, seed)
            print(f"{path}: {size} bytes")


if __name__ == "__main__":
    main()
