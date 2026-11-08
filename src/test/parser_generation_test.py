"""Tests for grammar contracts and generation failures, independent of the runtime parser."""
import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1] / "parser"
spec = importlib.util.spec_from_file_location("ll1_generator", ROOT / "tools/generate_ll1.py")
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)


class GenerationTests(unittest.TestCase):
    def test_real_grammar_reproduces_checked_in_tables(self):
        files = gen.generate(ROOT / "grammar/first_follow.csv", ROOT / "grammar/token_map.json",
                             ROOT.parent / "token/token.h")
        report = json.loads(files["manifest.json"])
        self.assertEqual((report["nonterminals"], report["productions"], report["filled_cells"]),
                         (52, 97, 253))
        for name, content in files.items():
            self.assertEqual((ROOT / "generated" / name).read_bytes(), content.encode())

    def test_first_of_nullable_prefix_uses_following_symbol(self):
        grammar = {"PROGRAMA": [("A", "B")], "A": [("a",), ()], "B": [("b",)]}
        first, follow, table, rules = gen.analyze(grammar, {"a": "A", "b": "B", "$": "EOF"})
        self.assertEqual(first["PROGRAMA"], {"a", "b"})
        self.assertEqual(follow["A"], {"b"})
        self.assertIn(("PROGRAMA", "b"), table)
        self.assertNotIn(("PROGRAMA", "$"), table)
        self.assertEqual(rules[table["A", "b"] - 1]["rhs"], [])

    def test_indirect_nullable_does_not_invent_direct_epsilon_rule(self):
        grammar = {"PROGRAMA": [("A",)], "A": [()]}
        _, _, table, rules = gen.analyze(grammar, {"$": "EOF"})
        self.assertEqual(rules[table["PROGRAMA", "$"] - 1]["rhs"], ["A"])
        self.assertEqual(rules[table["A", "$"] - 1]["rhs"], [])

    def test_empty_table_cell_is_different_from_epsilon(self):
        grammar = {"PROGRAMA": [("A", "x")], "A": [()]}
        _, _, table, rules = gen.analyze(grammar, {"x": "X", "y": "Y", "$": "EOF"})
        self.assertGreater(table["A", "x"], 0)
        self.assertEqual(rules[table["A", "x"] - 1]["rhs"], [])
        self.assertNotIn(("A", "y"), table)

    def test_first_first_conflict_is_rejected(self):
        grammar = {"PROGRAMA": [("x",), ("x", "y")]}
        with self.assertRaisesRegex(ValueError, "conflict"):
            gen.analyze(grammar, {"x": "X", "y": "Y", "$": "EOF"})

    def test_first_follow_conflict_is_rejected(self):
        grammar = {"PROGRAMA": [("A", "x")], "A": [("x",), ()]}
        with self.assertRaisesRegex(ValueError, "conflict"):
            gen.analyze(grammar, {"x": "X", "$": "EOF"})

    def test_unknown_symbol_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unknown symbol"):
            gen.analyze({"PROGRAMA": [("misspelled",)]}, {"$": "EOF"})

    def test_left_recursive_cycle_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "cycle"):
            gen.analyze({"PROGRAMA": [("PROGRAMA",)]}, {"$": "EOF"})

    def test_unreachable_rules_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "Unreachable"):
            gen.analyze({"PROGRAMA": [("x",)], "OTHER": [("x",)]}, {"x": "X", "$": "EOF"})

    def test_token_contract_drift_is_rejected(self):
        mapping = json.loads((ROOT / "grammar/token_map.json").read_text())
        mapping["id"] = "TOK_MISSPELLED"
        with self.assertRaisesRegex(ValueError, "differs"):
            gen.validate_tokens(mapping, ROOT.parent / "token/token.h")

    def test_reference_missing_column_is_rejected(self):
        grammar, _ = gen.read_grammar(ROOT / "grammar/first_follow.csv")
        terminals = json.loads((ROOT / "grammar/token_map.json").read_text())
        _, _, table, rules = gen.analyze(grammar, terminals)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "reference.csv"
            path.write_text("Não terminal,id\nPROGRAMA,\n")
            with self.assertRaisesRegex(ValueError, "columns"):
                gen.compare_reference(path, grammar, terminals, table, rules)

    def test_stale_first_follow_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "grammar.csv"
            with (ROOT / "grammar/first_follow.csv").open(newline="") as stream:
                reader = csv.DictReader(stream)
                rows, fields = list(reader), reader.fieldnames
            rows[0]["first"] = "id"
            with path.open("w", newline="") as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                writer.writerows(rows)
            with self.assertRaisesRegex(ValueError, "FIRST/FOLLOW"):
                gen.generate(path, ROOT / "grammar/token_map.json", ROOT.parent / "token/token.h")

    def test_reference_corrupt_cell_is_rejected(self):
        grammar, _ = gen.read_grammar(ROOT / "grammar/first_follow.csv")
        terminals = json.loads((ROOT / "grammar/token_map.json").read_text())
        _, _, table, rules = gen.analyze(grammar, terminals)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "reference.csv"
            with (ROOT / "generated/tabela_analise.csv").open(newline="") as stream:
                rows = list(csv.reader(stream))
            rows[1][1] = "TIPOBASE → float"
            with path.open("w", newline="") as stream:
                csv.writer(stream).writerows(rows)
            with self.assertRaisesRegex(ValueError, "mismatch"):
                gen.compare_reference(path, grammar, terminals, table, rules)


if __name__ == "__main__":
    unittest.main()
