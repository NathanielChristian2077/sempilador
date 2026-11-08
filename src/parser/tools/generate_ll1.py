#!/usr/bin/env python3
"""Validate the normalized grammar and emit deterministic LL(1) data, not a parser."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import re
import unicodedata

ROOT = Path(__file__).resolve().parents[1]
EPSILON = "ε"


def identifier(name):
    ascii_name = unicodedata.normalize("NFKD", name).encode("ascii", "ignore").decode()
    return re.sub(r"[^A-Za-z0-9_]", "_", ascii_name).upper()


def read_grammar(path):
    grammar, supplied = {}, {}
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        if reader.fieldnames != ["produção", "first", "follow", "regra"]:
            raise ValueError("CSV columns must be produção,first,follow,regra")
        for row in reader:
            lhs = row["produção"].strip()
            if not lhs or lhs in grammar:
                raise ValueError(f"Empty or repeated nonterminal: {lhs!r}")
            rule_lhs, rhs_text = row["regra"].split("→", 1)
            if rule_lhs.strip() != lhs:
                raise ValueError(f"Rule name differs from row: {lhs}")
            alternatives = []
            for text in rhs_text.strip().split(" | "):
                text = text.strip()
                if not text or text == "vazio":
                    raise ValueError(f"Use an explicit ε alternative: {lhs}")
                rhs = () if text == EPSILON else tuple(text.split())
                if EPSILON in rhs or "vazio" in rhs or rhs in alternatives:
                    raise ValueError(f"Invalid or duplicate alternative: {lhs} → {text}")
                alternatives.append(rhs)
            grammar[lhs] = alternatives
            supplied[lhs] = tuple(set(row[key].split(" | ")) if row[key] else set()
                                  for key in ("first", "follow"))
    if "PROGRAMA" not in grammar:
        raise ValueError("Missing start symbol PROGRAMA")
    ids = [identifier(n) for n in grammar]
    if len(set(ids)) != len(ids) or any(not n or n[0].isdigit() for n in ids):
        raise ValueError("Nonterminal names collide as C identifiers")
    return grammar, supplied


def first_sequence(rhs, grammar, first):
    result = set()
    for symbol in rhs:
        current = first[symbol] if symbol in grammar else {symbol}
        result.update(current - {EPSILON})
        if EPSILON not in current:
            return result
    return result | {EPSILON}


def analyze(grammar, terminals):
    if "$" not in terminals or set(grammar) & set(terminals):
        raise ValueError("Terminal/nonterminal collision or missing EOF")
    for lhs, alternatives in grammar.items():
        for rhs in alternatives:
            for symbol in rhs:
                if symbol not in grammar and symbol not in terminals:
                    raise ValueError(f"Unknown symbol {symbol!r} in {lhs}")
                if symbol == "$":
                    raise ValueError("EOF belongs to the initial parser stack, not a production")
    first = {n: set() for n in grammar}
    while True:
        previous = {n: set(s) for n, s in first.items()}
        for lhs, alternatives in grammar.items():
            for rhs in alternatives:
                first[lhs].update(first_sequence(rhs, grammar, first))
        if previous == first:
            break
    if any(not values for values in first.values()):
        raise ValueError("Unproductive nonterminal or non-consuming cycle")
    reachable, pending = set(), ["PROGRAMA"]
    while pending:
        lhs = pending.pop()
        if lhs in reachable:
            continue
        reachable.add(lhs)
        pending.extend(s for rhs in grammar[lhs] for s in rhs if s in grammar)
    if reachable != set(grammar):
        raise ValueError(f"Unreachable nonterminals: {sorted(set(grammar) - reachable)}")
    follow = {n: set() for n in grammar}
    follow["PROGRAMA"].add("$")
    while True:
        previous = {n: set(s) for n, s in follow.items()}
        for lhs, alternatives in grammar.items():
            for rhs in alternatives:
                for index, symbol in enumerate(rhs):
                    if symbol not in grammar:
                        continue
                    tail = first_sequence(rhs[index + 1:], grammar, first)
                    follow[symbol].update(tail - {EPSILON})
                    if EPSILON in tail:
                        follow[symbol].update(follow[lhs])
        if previous == follow:
            break
    table, productions = {}, []
    for lhs, alternatives in grammar.items():
        for rhs in alternatives:
            production = {"id": len(productions) + 1, "lhs": lhs, "rhs": list(rhs),
                          "key": lhs + " → " + (" ".join(rhs) if rhs else EPSILON)}
            productions.append(production)
            selection = first_sequence(rhs, grammar, first)
            lookaheads = selection - {EPSILON}
            if EPSILON in selection:
                lookaheads |= follow[lhs]
            for token in lookaheads:
                cell = lhs, token
                if cell in table:
                    earlier = productions[table[cell] - 1]["key"]
                    raise ValueError(f"LL(1) conflict at {cell}: {earlier} / {production['key']}")
                table[cell] = production["id"]
    if len(productions) >= 65536 or len(grammar) >= 32768:
        raise ValueError("Grammar exceeds the generated integer representation")
    if sum(len(p["rhs"]) for p in productions) >= 65536:
        raise ValueError("RHS pool exceeds uint16_t offsets")
    if any(len(p["rhs"]) >= 65536 for p in productions):
        raise ValueError("Production exceeds uint16_t length")
    return first, follow, table, productions


def validate_tokens(terminals, header):
    values = list(terminals.values())
    if len(set(values)) != len(values) or terminals.get("$") != "TOK_EOF":
        raise ValueError("Token mapping must be one-to-one and map $ to TOK_EOF")
    text = Path(header).read_text(encoding="utf-8")
    match = re.search(r"typedef\s+enum\s*\{([^}]+)\}\s*TokenKind", text, re.S)
    if not match:
        raise ValueError("TokenKind enum not found")
    names = re.findall(r"\bTOK_[A-Z0-9_]+\b", match[1])
    if len(names) != len(set(names)) or names[-1] != "TOK_COMMENT":
        raise ValueError("Unsupported TokenKind contract")
    if set(values) != set(names) - {"TOK_COMMENT", "TOK_INVALID"}:
        raise ValueError("Token mapping differs from TokenKind; update the contract explicitly")
    return names


def compare_reference(path, grammar, terminals, table, productions):
    with Path(path).open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.reader(stream)
        headers = next(reader)[1:]
        rows = list(reader)
    if len(headers) != len(set(headers)) or set(headers) != set(terminals):
        raise ValueError("Reference columns differ from grammar terminals")
    if len(rows) != len(grammar) or {r[0] for r in rows} != set(grammar):
        raise ValueError("Reference rows differ from grammar nonterminals")
    for row in rows:
        if len(row) != len(headers) + 1:
            raise ValueError("Malformed reference row")
        for terminal, given in zip(headers, row[1:]):
            pid = table.get((row[0], terminal))
            expected = productions[pid - 1]["key"] if pid else ""
            if given != expected:
                raise ValueError(f"Reference mismatch at {(row[0], terminal)}")


def generate(grammar_path, mapping_path, token_header, reference=None):
    grammar, supplied = read_grammar(grammar_path)
    terminals = json.loads(Path(mapping_path).read_text(encoding="utf-8"))
    names = validate_tokens(terminals, token_header)
    first, follow, table, productions = analyze(grammar, terminals)
    for lhs in grammar:
        if supplied[lhs] != (first[lhs], follow[lhs]):
            raise ValueError(f"Supplied FIRST/FOLLOW differs from recalculation: {lhs}")
    if reference is not None:
        compare_reference(reference, grammar, terminals, table, productions)
    fingerprint = hashlib.sha256(json.dumps(
        {"grammar": grammar, "terminals": terminals, "token_names": names},
        ensure_ascii=False, separators=(",", ":")).encode()).hexdigest()
    nts = {n: "LL1_NT_" + identifier(n) for n in grammar}
    pool_size = sum(len(p["rhs"]) for p in productions)
    h = ["/* Generated by parser/tools/generate_ll1.py; do not edit. */",
         "#ifndef SEMPILADOR_LL1_TABLES_H", "#define SEMPILADOR_LL1_TABLES_H",
         '#include "../../token/token.h"', "#include <stdbool.h>", "#include <stdint.h>",
         "typedef enum {"]
    h += [f"    {enum_name} = {i}," for i, enum_name in enumerate(nts.values())]
    h += [f"    LL1_NONTERMINAL_COUNT = {len(grammar)}", "} LL1Nonterminal;",
          "typedef uint16_t LL1Symbol;", "typedef uint16_t LL1ProductionId;",
          "#define LL1_ERROR ((LL1ProductionId)0)",
          "#define LL1_NONTERMINAL_FLAG UINT16_C(0x8000)",
          "#define LL1_NONTERMINAL(n) ((LL1Symbol)(LL1_NONTERMINAL_FLAG | (n)))",
          f"#define LL1_PRODUCTION_COUNT {len(productions)}",
          f"#define LL1_RHS_COUNT {pool_size}",
          "#define LL1_TOKEN_COLUMN_COUNT (TOK_COMMENT + 1)",
          f'#define LL1_GRAMMAR_SHA256 "{fingerprint}"',
          "/* rhs_length == 0 is epsilon; production ID 0 is an error, not epsilon. */",
          "typedef struct { uint16_t lhs, rhs_offset, rhs_length; } LL1Production;",
          "extern const LL1ProductionId ll1_table[LL1_NONTERMINAL_COUNT][LL1_TOKEN_COLUMN_COUNT];",
          "extern const LL1Production ll1_productions[LL1_PRODUCTION_COUNT + 1];",
          "extern const LL1Symbol ll1_rhs[LL1_RHS_COUNT];",
          "extern const uint64_t ll1_first[LL1_NONTERMINAL_COUNT];",
          "extern const uint64_t ll1_follow[LL1_NONTERMINAL_COUNT];",
          "extern const uint64_t ll1_expected[LL1_NONTERMINAL_COUNT];",
          "extern const bool ll1_nullable[LL1_NONTERMINAL_COUNT];",
          "extern const char *const ll1_nonterminal_names[LL1_NONTERMINAL_COUNT];",
          "extern const char *const ll1_production_keys[LL1_PRODUCTION_COUNT + 1];",
          "#endif", ""]
    c = ['/* Generated by parser/tools/generate_ll1.py; do not edit. */', '#include "ll1_tables.h"',
         '_Static_assert(TOK_COMMENT < 64, "Token masks require fewer than 64 kinds");',
         'const LL1ProductionId ll1_table[LL1_NONTERMINAL_COUNT][LL1_TOKEN_COLUMN_COUNT] = {']
    for lhs in grammar:
        cells = ", ".join(f"[{token}] = {table[lhs, term]}" for term, token in terminals.items()
                          if (lhs, term) in table)
        c.append(f"    [{nts[lhs]}] = {{{cells}}},")
    c += ["};", "const LL1Production ll1_productions[LL1_PRODUCTION_COUNT + 1] = {"]
    offset = 0
    for p in productions:
        c.append(f"    [{p['id']}] = {{{nts[p['lhs']]}, {offset}, {len(p['rhs'])}}},")
        offset += len(p["rhs"])
    c += ["};", "const LL1Symbol ll1_rhs[LL1_RHS_COUNT] = {"]
    for p in productions:
        if p["rhs"]:
            rhs = [f"LL1_NONTERMINAL({nts[s]})" if s in grammar else terminals[s] for s in p["rhs"]]
            c.append(f"    {', '.join(rhs)}, /* {p['id']} */")
    c.append("};")
    for name, sets in [("first", first), ("follow", follow),
                       ("expected", {n: {t for t in terminals if (n, t) in table} for n in grammar})]:
        c.append(f"const uint64_t ll1_{name}[LL1_NONTERMINAL_COUNT] = {{")
        for lhs in grammar:
            bits = [f"(UINT64_C(1) << {token})" for term, token in terminals.items() if term in sets[lhs]]
            c.append(f"    [{nts[lhs]}] = {' | '.join(bits) if bits else 'UINT64_C(0)'},")
        c.append("};")
    c.append("const bool ll1_nullable[LL1_NONTERMINAL_COUNT] = {")
    c += [f"    [{nts[n]}] = {'true' if EPSILON in first[n] else 'false'}," for n in grammar]
    c += ["};", "const char *const ll1_nonterminal_names[LL1_NONTERMINAL_COUNT] = {"]
    c += [f"    [{nts[n]}] = {json.dumps(n, ensure_ascii=False)}," for n in grammar]
    c += ["};", 'const char *const ll1_production_keys[LL1_PRODUCTION_COUNT + 1] = {', '    [0] = "ERROR",']
    c += [f"    [{p['id']}] = {json.dumps(p['key'], ensure_ascii=False)}," for p in productions]
    c += ["};", ""]
    matrix = io.StringIO(newline="")
    writer = csv.writer(matrix, lineterminator="\n")
    writer.writerow(["Não terminal", *terminals])
    for n in grammar:
        writer.writerow([n, *(productions[table[n, t] - 1]["key"] if (n, t) in table else "" for t in terminals)])
    report = {"schema": 1, "grammar_sha256": fingerprint, "nonterminals": len(grammar),
              "grammar_terminals": len(terminals), "token_kinds": len(names),
              "productions": len(productions), "filled_cells": len(table), "rhs_symbols": pool_size,
              "nullable_nonterminals": sum(EPSILON in first[n] for n in grammar),
              "dense_uint16_bytes": len(grammar) * len(names) * 2,
              "reference_verified": reference is not None, "production_catalog": productions}
    return {"ll1_tables.h": "\n".join(h), "ll1_tables.c": "\n".join(c),
            "tabela_analise.csv": matrix.getvalue(),
            "manifest.json": json.dumps(report, ensure_ascii=False, indent=2) + "\n"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--grammar", type=Path, default=ROOT / "grammar/first_follow.csv")
    parser.add_argument("--tokens", type=Path, default=ROOT / "grammar/token_map.json")
    parser.add_argument("--token-header", type=Path, default=ROOT.parent / "token/token.h")
    parser.add_argument("--reference", type=Path, default=None)
    parser.add_argument("--out", type=Path, default=ROOT / "generated")
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    try:
        files = generate(args.grammar, args.tokens, args.token_header, args.reference)
        for name, content in files.items():
            path = args.out / name
            if args.check:
                if not path.is_file() or path.read_bytes() != content.encode("utf-8"):
                    raise ValueError(f"Generated file differs: {path}")
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content.encode("utf-8"))
    except (ValueError, KeyError, OSError) as error:
        parser.exit(1, f"LL1: {error}\n")
    checks = "grammar, FIRST/FOLLOW, token contract, "
    if args.reference is not None:
        checks += "reference, "
    print("LL1: " + checks + "generated files OK")


if __name__ == "__main__":
    main()
