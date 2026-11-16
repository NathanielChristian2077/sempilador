#!/usr/bin/env python3
"""Build a clean source copy without re2c, including newer .re timestamps."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
OPTIONS = None


class MakefileTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="sempilador-build-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name) / "project with spaces"
        shutil.copytree(ROOT / "src", self.root / "src",
                        ignore=shutil.ignore_patterns("bin", "__pycache__", "*.ll"))
        shutil.copyfile(ROOT / "Makefile", self.root / "Makefile")
        self.environment = os.environ.copy()
        for name in ("MAKEFLAGS", "MFLAGS", "MAKEOVERRIDES"):
            self.environment.pop(name, None)

    def make(self, *targets, expected=0):
        command = [OPTIONS.make, *targets, "BUILD_DIR=bin", f"CC={OPTIONS.cc}",
                   f"CFLAGS={OPTIONS.cflags}", f"SHELL={OPTIONS.shell}",
                   f"PYTHON={sys.executable}", "RE2C=missing-re2c-for-build-regression"]
        result = subprocess.run(command, cwd=self.root, env=self.environment,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=120)
        text = result.stdout.decode("utf-8", errors="replace")
        if expected == 0:
            self.assertEqual(result.returncode, 0, text)
        else:
            self.assertNotEqual(result.returncode, 0, text)
        return text

    def test_newer_lexer_source_does_not_require_re2c(self):
        source = self.root / "src/lexer/lexer.re"
        stamp = time.time() + 10
        os.utime(source, (stamp, stamp))
        self.make()
        suffix = ".exe" if os.name == "nt" else ""
        executable = self.root / "src/bin" / ("sempilador" + suffix)
        self.assertTrue(executable.is_file())
        built = executable.stat().st_mtime_ns
        self.make("build")
        self.assertEqual(executable.stat().st_mtime_ns, built)
        self.make("run", "FILE=src/test/valid.csim")
        self.assertTrue((self.root / "src/test/valid.ll").is_file())
        self.make("clean")
        self.assertFalse(executable.parent.exists())
        self.make("clean")

    def test_missing_generated_lexer_has_an_explicit_error(self):
        (self.root / "src/lexer/lexer.c").unlink()
        output = self.make("build", expected=1)
        self.assertIn("Missing lexer/lexer.c", output)
        self.assertIn("make regen-lexer", output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--make", default="make")
    parser.add_argument("--cc", default="gcc")
    parser.add_argument("--cflags", default="-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror")
    parser.add_argument("--shell", default="cmd.exe" if os.name == "nt" else "/bin/sh")
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
