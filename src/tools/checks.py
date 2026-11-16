#!/usr/bin/env python3
"""Portable CLI, lexer-generation and LLVM integration checks."""
import argparse
import difflib
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def run(command, *, expected=0, input_data=b"", log=None):
    result = subprocess.run(command, input=input_data, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE, timeout=60)
    if log is not None:
        Path(log).write_bytes(result.stdout)
    if result.returncode != expected:
        raise RuntimeError(f"{command[0]}: exit {result.returncode}, expected {expected}\n"
                           + result.stdout.decode("utf-8", errors="replace")
                           + result.stderr.decode("utf-8", errors="replace"))
    return result.stdout


def cli(args):
    compiler = str(Path(args.compiler).resolve())
    directory = Path(args.directory)
    directory.mkdir(parents=True, exist_ok=True)
    cases = [
        ("valid", "valid", 0, [b"programa valido", b"LLVM gerado em"]),
        ("semantic_error", "semantic_error", 1, [b"Linha 4, coluna 5: tipos incompat"]),
        ("symbol", "build_error", 1, [b"Linha 4, coluna 1: esperado", b"contador"]),
        ("invalid_number", "invalid_number", 1,
         [b"Linha 1, coluna 12: erro lexico", b'encontrado token invalido "2D"']),
    ]
    for source, name, status, fragments in cases:
        ir = directory / f"{source}.ll"
        ir.unlink(missing_ok=True)
        output = run([compiler, f"test/{source}.csim", str(ir)], expected=status,
                     log=directory / f"test_{name}.out")
        if any(fragment not in output for fragment in fragments):
            raise RuntimeError(f"CLI: missing diagnostic for {source}")
        if ir.exists() != (status == 0):
            raise RuntimeError(f"CLI: unexpected IR state for {source}")
    print("CLI: valid program and negative diagnostics OK")


def codegen(args):
    compiler = str(Path(args.compiler).resolve())
    directory = Path(args.directory) / "codegen"
    directory.mkdir(parents=True, exist_ok=True)
    sources = sorted(Path("test/codegen").glob("*.csim"))
    if not sources:
        raise RuntimeError("codegen: no test programs found")
    for source in sources:
        target = directory / source.stem
        ir = target.with_suffix(".ll")
        run([compiler, str(source), str(ir)], log=target.with_suffix(".log"))
        run([args.opt, "-passes=verify", "-disable-output", str(ir)])
        input_file = source.with_suffix(".in")
        output = run([args.lli, str(ir)],
                     input_data=input_file.read_bytes() if input_file.exists() else b"",
                     log=target.with_suffix(".out"))
        expected = source.with_suffix(".out").read_bytes()
        if output.replace(b"\r\n", b"\n") != expected.replace(b"\r\n", b"\n"):
            raise RuntimeError(f"codegen: output differs for {source.stem}")
        print(f"codegen: {source.stem} OK")


def lexer(args):
    output = Path(args.output)
    descriptor, temporary = tempfile.mkstemp(prefix=output.name + ".", suffix=".tmp",
                                            dir=output.parent)
    os.close(descriptor)
    temporary = Path(temporary)
    try:
        command = args.command[1:] if args.command[:1] == ["--"] else args.command
        if not command:
            raise RuntimeError("lexer: missing re2c command")
        run(command + ["-o", str(temporary), "lexer/lexer.re"])
        if args.check:
            generated = temporary.read_bytes()
            if output.read_bytes() != generated:
                difference = difflib.unified_diff(
                    output.read_text(encoding="utf-8").splitlines(True),
                    generated.decode("utf-8").splitlines(True),
                    fromfile=str(output), tofile="regenerated lexer")
                sys.stderr.writelines(difference)
                raise RuntimeError("lexer: generated file is stale; run make regen-lexer")
            print("lexer: generated file OK")
        else:
            temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="action", required=True)
    for name in ("cli", "codegen"):
        check = commands.add_parser(name)
        check.add_argument("--compiler", required=True)
        check.add_argument("--directory", required=True)
        if name == "codegen":
            check.add_argument("--opt", default="opt")
            check.add_argument("--lli", default="lli")
    generator = commands.add_parser("lexer")
    generator.add_argument("--output", required=True)
    generator.add_argument("--check", action="store_true")
    generator.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    try:
        {"cli": cli, "codegen": codegen, "lexer": lexer}[args.action](args)
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
