#!/usr/bin/env python3
"""Check std.sqlite against the sqlite3 module of Python over the same SQL.

The program under test (tests/fixtures/codegen_sqlite_driver.r) reads one command per line and
prints what std.sqlite returned. This harness runs the same commands on an in-memory database of
the sqlite3 module of Python, in autocommit mode with foreign keys on as std.sqlite opens a
database, and compares column names, values with their storage classes (reals bit for bit), the
changes and last row id of a statement, the transaction state and the extended result codes of
failures. The refusals of std.sqlite itself (no statement, several statements, a wrong number of
values, text that is not UTF-8, a zero byte in SQL) carry native code 0.
"""
from __future__ import annotations

import argparse
import math
import random
import re
import sqlite3
import struct
import subprocess
import sys
from pathlib import Path

PRIMARY = {1: "sql", 5: "busy", 6: "locked", 8: "read_only", 9: "interrupted", 10: "io",
           11: "corrupt", 13: "full", 14: "cannot_open", 18: "too_big", 19: "constraint",
           20: "mismatch", 21: "misuse", 25: "range", 26: "corrupt"}


class Refusal(Exception):
    """A refusal of std.sqlite, which Python reports as another exception."""

    def __init__(self, code: str):
        super().__init__(code)
        self.code = code


def encode_value(value) -> str:
    if value is None:
        return "n"
    if isinstance(value, int):
        return f"i{value}"
    if isinstance(value, float):
        return "r" + repr(value)
    if isinstance(value, str):
        data = value.encode("utf-8")
        return "t" + (data.hex() if data else "-")
    data = bytes(value)
    return "b" + (data.hex() if data else "-")


def value_key(value):
    """A value of Python in the form both sides are compared in."""
    if value is None:
        return None
    if isinstance(value, int):
        return ("integer", value)
    if isinstance(value, float):
        return ("real", struct.pack("<d", value))
    if isinstance(value, str):
        return ("text", value.encode("utf-8"))
    return ("blob", bytes(value))


def token_key(token: str):
    """A value that the driver printed in the same form."""
    kind, rest = token[0], token[1:]
    if kind == "n":
        return None
    if kind == "i":
        return ("integer", int(rest))
    if kind == "r":
        return ("real", struct.pack("<d", float(rest)))
    data = b"" if rest == "-" else bytes.fromhex(rest)
    return ("text" if kind == "t" else "blob", data)


def hex_or_dash(data: bytes) -> str:
    return data.hex() if data else "-"


class Oracle:
    """The expected answer of each command, from the sqlite3 module of Python."""

    def __init__(self):
        self.db = sqlite3.connect(":memory:", isolation_level=None)
        self.db.execute("PRAGMA foreign_keys = ON")
        self.prepared = ""

    def rows(self, sql: str, values: list) -> list:
        try:
            cursor = self.db.execute(sql, values)
            found = cursor.fetchall()
        except sqlite3.ProgrammingError as error:
            text = str(error)
            if "one statement at a time" in text:
                raise Refusal("several_statements") from error
            if "bindings" in text:
                raise Refusal("parameter_count") from error
            raise
        except sqlite3.OperationalError as error:
            if "decode" in str(error).lower():
                raise Refusal("invalid_text") from error
            raise
        names = [] if cursor.description is None else [column[0] for column in cursor.description]
        block = ["columns" + "".join(" " + hex_or_dash(name.encode("utf-8")) for name in names)]
        block.extend(("row", [value_key(value) for value in row]) for row in found)
        block.append("end")
        return block

    def run(self, case: "Case") -> list:
        try:
            if case.command == "script":
                for piece in [part for part in case.argument.split(";") if part.strip()]:
                    self.db.execute(piece).fetchall()
                return ["ok"]
            if case.command == "query":
                return self.rows(case.argument, case.values)
            if case.command == "execute":
                # execute steps through the rows without reading them, so no text is decoded.
                self.db.text_factory = bytes
                try:
                    self.rows(case.argument, case.values)
                finally:
                    self.db.text_factory = str
                changes, last = self.db.execute("SELECT changes(), last_insert_rowid()").fetchone()
                return [f"done {changes} {last}"]
            if case.command == "prepare":
                try:
                    # EXPLAIN compiles the statement without running it; its parameters stay
                    # unbound, which Python refuses only after compiling.
                    self.db.execute("EXPLAIN " + case.argument).fetchall()
                except sqlite3.ProgrammingError as error:
                    if "bindings" not in str(error):
                        raise
                self.prepared = case.argument
                return ["ok"]
            if case.command == "run":
                return self.rows(self.prepared, case.values)
            if case.command == "begin":
                self.db.execute(f"BEGIN {case.argument.upper()}")
                return ["ok"]
            if case.command in ("commit", "rollback"):
                self.db.execute(case.command.upper())
                return ["ok"]
            if case.command == "state":
                return ["true" if self.db.in_transaction else "false"]
        except Refusal as refusal:
            return [f"error {refusal.code} 0"]
        except sqlite3.Error as error:
            code = error.sqlite_errorcode
            return [f"error {PRIMARY.get(code & 255, 'other')} {code}"]
        raise ValueError(case.command)


class Case:
    def __init__(self, command: str, argument: str = "", values: list | None = None,
                 expect: list | None = None):
        self.command, self.argument, self.values = command, argument, values or []
        self.expect = expect

    def text(self) -> str:
        parts = [self.command]
        if self.command in ("script", "query", "execute", "prepare"):
            parts.append(hex_or_dash(self.argument.encode("utf-8")))
        elif self.command == "begin":
            parts.append(self.argument)
        parts.extend(encode_value(value) for value in self.values)
        return " ".join(parts)


def random_text(rng: random.Random) -> str:
    alphabet = ["a", "Z", " ", "é", "ß", "Ж", "中", "😀", "\u0000", "\t", "'", '"', "\U0010fffd"]
    return "".join(rng.choice(alphabet) for _ in range(rng.randrange(0, 24)))


def random_value(rng: random.Random):
    kind = rng.randrange(5)
    if kind == 0:
        return None
    if kind == 1:
        return rng.choice([0, 1, -1, 2**63 - 1, -2**63, rng.randrange(-2**63, 2**63),
                           rng.randrange(-1000, 1000)])
    if kind == 2:
        return rng.choice([0.0, -0.0, 0.5, -1e-300, 1e308, 5e-324, math.inf, -math.inf,
                           rng.uniform(-1e6, 1e6), struct.unpack("<d", rng.randbytes(8))[0]])
    if kind == 3:
        return random_text(rng)
    return rng.randbytes(rng.randrange(0, 40))


def build_cases() -> list[Case]:
    rng = random.Random(29)
    cases = [Case("state"), Case("script", "CREATE TABLE v(id INTEGER PRIMARY KEY, x)")]
    # Values of every storage class, through a statement prepared for each and through one
    # prepared statement run again; NaN is stored as NULL.
    edge = [None, 0, 1, -1, 2**63 - 1, -2**63, 0.0, -0.0, 1.5, -2.25, 1e308, 5e-324, math.inf,
            -math.inf, math.nan, "", "käse", "😀\u0000end", "x" * 10000, b"", b"\x00\xff",
            bytes(range(256)) * 117]
    cases += [Case("execute", "INSERT INTO v(x) VALUES (?)", [value]) for value in edge]
    # A line of the driver holds at most 64 KiB, so a blob of 1 MiB comes from SQL.
    cases.append(Case("execute", "INSERT INTO v(x) VALUES (zeroblob(1048576))"))
    cases.append(Case("prepare", "INSERT INTO v(x) VALUES (?1)"))
    cases += [Case("run", values=[random_value(rng)]) for _ in range(300)]
    cases.append(Case("query", "SELECT id, x, typeof(x), CASE WHEN typeof(x) IN ('text', 'blob') "
                               "THEN length(x) END AS size FROM v ORDER BY id"))
    cases.append(Case("prepare", "SELECT x, typeof(x) FROM v WHERE id = ?"))
    cases += [Case("run", values=[row_id]) for row_id in (1, 5, 17, 23, 999)]
    # Affinity converts what columns of a declared type receive.
    cases.append(Case("script", "CREATE TABLE aff(i INTEGER, r REAL, t TEXT, n NUMERIC, b BLOB)"))
    for row in [["12", "3.5", 7, "1e3", "x"], [3.0, 2, 2.5, "12abc", 4],
                ["0x10", "1e400", None, " 7 ", b"\x01"], [2**63 - 1, "-0", -0.0, "9223372036854775808", 1.0]]:
        cases.append(Case("execute", "INSERT INTO aff VALUES (?, ?, ?, ?, ?)", row))
    cases.append(Case("query", "SELECT i, typeof(i), r, typeof(r), t, typeof(t), n, typeof(n), b, "
                               "typeof(b) FROM aff"))
    # Expressions over values of every class.
    for _ in range(60):
        operator = rng.choice(["+", "-", "*", "/", "%", "||", "=", "<", "IS", "AND"])
        cases.append(Case("query", f"SELECT ?1 {operator} ?2 AS result, typeof(?1 {operator} ?2) AS kind",
                          [random_value(rng), random_value(rng)]))
    for sql in ["SELECT 9223372036854775807 + 1", "SELECT 1 / 0, 1.0 / 0", "SELECT abs(-9223372036854775808)",
                "SELECT CAST('12x' AS INTEGER), CAST(x'41' AS TEXT), CAST(1.9 AS INTEGER)",
                "SELECT hex(zeroblob(3)), quote(x'00ff'), typeof(NULL)", "SELECT 1 AS \"a b\", 2 AS ''",
                "SELECT count(*), sum(id), total(id), min(id), max(typeof(x)) FROM v",
                "INSERT INTO v(x) VALUES ('returned') RETURNING id, x"]:
        cases.append(Case("query", sql))
    # Failures of SQLite and refusals of the module.
    cases.append(Case("script", "CREATE TABLE person(name TEXT PRIMARY KEY, email TEXT UNIQUE, "
                                "age INTEGER NOT NULL CHECK (age >= 0)); "
                                "CREATE TABLE pet(owner TEXT REFERENCES person(name), id INTEGER PRIMARY KEY)"))
    for sql, values in [("INSERT INTO person VALUES (?, ?, ?)", ["ada", "a@x", 36]),
                        ("INSERT INTO person VALUES (?, ?, ?)", ["ada", "b@x", 1]),
                        ("INSERT INTO person VALUES (?, ?, ?)", ["bob", "a@x", 1]),
                        ("INSERT INTO person VALUES (?, ?, ?)", ["cyd", "c@x", None]),
                        ("INSERT INTO person VALUES (?, ?, ?)", ["dan", "d@x", -1]),
                        ("INSERT INTO pet VALUES (?, ?)", ["nobody", 1]),
                        ("INSERT INTO pet VALUES (?, ?)", ["ada", "seven"]),
                        ("INSERT INTO pet VALUES (?, ?)", ["ada", 7]),
                        ("DELETE FROM person WHERE name = ?", ["ada"]),
                        ("UPDATE person SET age = age + 1", []),
                        ("SELEC 1", []), ("SELECT * FROM nowhere", []), ("SELECT 1; SELECT 2", []),
                        ("SELECT ?", []), ("SELECT ?", [1, 2]), ("SELECT ?2", [1]),
                        ("SELECT CAST(x'ff' AS TEXT)", [])]:
        cases.append(Case("execute", sql, values))
    cases += [Case("query", "SELECT 1 AS one; -- trailing comment"),
              Case("query", "SELECT 2 AS two; /* block */  "),
              Case("execute", "  -- nothing", expect=["error no_statement 0"]),
              Case("query", "", expect=["error no_statement 0"]),
              Case("query", "SELECT 1\x00", expect=["error invalid_text 0"]),
              Case("prepare", "SELEC 1"),
              Case("run", values=[1])]
    # Transactions; a failed statement leaves the transaction open.
    cases += [Case("script", "CREATE TABLE entry(text TEXT UNIQUE)"), Case("state"),
              Case("begin", "deferred"), Case("state"),
              Case("execute", "INSERT INTO entry VALUES (?)", ["discarded"]), Case("rollback"),
              Case("state"), Case("begin", "immediate"),
              Case("execute", "INSERT INTO entry VALUES (?)", ["kept"]),
              Case("execute", "INSERT INTO entry VALUES (?)", ["kept"]), Case("state"),
              Case("begin", "exclusive"), Case("commit"), Case("commit"), Case("rollback"),
              Case("query", "SELECT text FROM entry"),
              Case("script", "INSERT INTO entry VALUES ('a'); INSERT INTO nowhere VALUES (1); "
                             "INSERT INTO entry VALUES ('b')"),
              Case("query", "SELECT text FROM entry ORDER BY text"),
              Case("script", "SELECT 1\x00", expect=["error invalid_text 0"])]
    # std.sqlite calls the SQLite that the build downloads and compiles with the options of
    # library/native/sqlite/README.md,
    # whatever SQLite Python links (Library R-SLIB-IDB-0017), so these answers do not come from
    # the oracle: the version, a double-quoted name that is only an identifier (SQLITE_DQS=0),
    # mathematical functions, full-text search, and no loadable extensions.
    def columns(*names: str) -> str:
        return "columns" + "".join(" " + hex_or_dash(name.encode("utf-8")) for name in names)

    cases += [Case("query", "SELECT sqlite_version() AS version",
                   expect=[columns("version"), ("row", [("text", vendored_version().encode())]), "end"]),
              Case("query", 'SELECT "no such column"', expect=["error sql 1"]),
              Case("query", "SELECT sqrt(16.0) AS root, pow(2, 10) AS power",
                   expect=[columns("root", "power"),
                           ("row", [value_key(4.0), value_key(1024.0)]), "end"]),
              Case("script", "CREATE VIRTUAL TABLE notes USING fts5(body)", expect=["ok"]),
              Case("execute", "INSERT INTO notes(body) VALUES (?)", ["the quick brown fox"],
                   expect=["done 1 1"]),
              Case("execute", "INSERT INTO notes(body) VALUES (?)", ["a lazy dog"],
                   expect=["done 1 2"]),
              Case("query", "SELECT body FROM notes WHERE notes MATCH 'quick'",
                   expect=[columns("body"), ("row", [value_key("the quick brown fox")]), "end"]),
              Case("query", "SELECT load_extension('anything')", expect=["error sql 1"])]
    return cases


# sqlite3.h of the SQLite that std.sqlite is built with; main sets it from --header.
SQLITE_HEADER = Path()


def vendored_version() -> str:
    header = SQLITE_HEADER
    found = re.search(r'#define SQLITE_VERSION\s+"([0-9.]+)"', header.read_text(encoding="utf-8"))
    if found is None:
        raise SystemExit(f"no SQLITE_VERSION in {header}")
    return found.group(1)


def split_responses(lines: list[str], cases: list[Case]) -> list[list[str]]:
    out, at = [], 0
    for _ in cases:
        first = lines[at]
        at += 1
        block = [first]
        if first.startswith("columns"):
            while lines[at] != "end":
                block.append(lines[at])
                at += 1
            block.append("end")
            at += 1
        out.append(block)
    if at != len(lines):
        raise SystemExit(f"{len(lines) - at} extra output lines")
    return out


def matches(want: list, got: list[str]) -> bool:
    if len(want) != len(got):
        return False
    for line_want, line_got in zip(want, got):
        if isinstance(line_want, tuple):
            tokens = line_got.split(" ")
            if tokens[0] != "row" or [token_key(token) for token in tokens[1:]] != line_want[1]:
                return False
        elif line_want != line_got:
            return False
    return True


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--executable", required=True)
    parser.add_argument("--header", required=True, help="sqlite3.h of the SQLite std.sqlite is built with")
    arguments = parser.parse_args()
    global SQLITE_HEADER
    SQLITE_HEADER = Path(arguments.header)
    cases = build_cases()
    oracle = Oracle()
    expected = [case.expect if case.expect is not None else oracle.run(case) for case in cases]
    program = subprocess.run([arguments.executable], input="".join(case.text() + "\n" for case in cases),
                             capture_output=True, text=True, timeout=240)
    if program.returncode != 0:
        print(program.stderr[-2000:], file=sys.stderr)
        raise SystemExit(f"driver exited with {program.returncode}")
    actual = split_responses(program.stdout.splitlines(), cases)
    failures = 0
    rows = 0
    for case, want, got in zip(cases, expected, actual):
        rows += sum(1 for line in want if isinstance(line, tuple))
        if not matches(want, got):
            failures += 1
            if failures <= 10:
                print(f"MISMATCH {case.command} {case.argument[:80]!r} {case.values[:2]!r}\n"
                      f"  expected {[line if isinstance(line, str) else line[1] for line in want][:6]}\n"
                      f"  actual   {got[:6]}", file=sys.stderr)
    print(f"sqlite vectors: {len(cases) - failures}/{len(cases)} commands agree, {rows} rows compared "
          f"(std.sqlite on SQLite {vendored_version()}, oracle on SQLite {sqlite3.sqlite_version})")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
