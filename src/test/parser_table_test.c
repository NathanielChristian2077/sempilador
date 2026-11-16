#include "../parser/generated/ll1_tables.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    size_t cells = 0, epsilon = 0;
    for (size_t n = 0; n < LL1_NONTERMINAL_COUNT; ++n) {
        uint64_t expected = 0;
        assert(ll1_nonterminal_names[n] != NULL);
        for (unsigned token = 0; token < LL1_TOKEN_COLUMN_COUNT; ++token) {
            LL1ProductionId id = ll1_table[n][token];
            if (id == LL1_ERROR) continue;
            ++cells;
            assert(id <= LL1_PRODUCTION_COUNT);
            assert(ll1_productions[id].lhs == n);
            expected |= UINT64_C(1) << token;
        }
        assert(expected == ll1_expected[n]);
        assert(ll1_table[n][TOK_INVALID] == LL1_ERROR);
        assert(ll1_table[n][TOK_COMMENT] == LL1_ERROR);
    }
    for (unsigned id = 1; id <= LL1_PRODUCTION_COUNT; ++id) {
        LL1Production p = ll1_productions[id];
        assert(p.lhs < LL1_NONTERMINAL_COUNT);
        assert((size_t)p.rhs_offset + p.rhs_length <= LL1_RHS_COUNT);
        assert(ll1_production_keys[id] != NULL);
        if (p.rhs_length == 0) ++epsilon;
    }
    for (size_t i = 0; i < LL1_RHS_COUNT; ++i) {
        LL1Symbol s = ll1_rhs[i];
        if (s & LL1_NONTERMINAL_FLAG)
            assert((s & ~LL1_NONTERMINAL_FLAG) < LL1_NONTERMINAL_COUNT);
        else
            assert(s < TOK_COMMENT && s != TOK_INVALID);
    }
    assert(cells == 253 && epsilon == 22);
    LL1ProductionId empty = ll1_table[LL1_NT_COMANDOS][TOK_RBRACE];
    assert(empty != LL1_ERROR && ll1_productions[empty].rhs_length == 0);
    assert(ll1_table[LL1_NT_COMANDO][TOK_FOR] != LL1_ERROR);
    assert(ll1_table[LL1_NT_COMANDO][TOK_ELSE] == LL1_ERROR);
    printf("LL1 tables: %zu cells; %zu epsilon rules; dense matrix %zu bytes\n",
           cells, epsilon, sizeof(ll1_table));
    return 0;
}
