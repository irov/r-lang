"""Deterministic regex differential and malformed-pattern tests through executable R code."""

from __future__ import annotations

import argparse
import random
import re
import subprocess


SEED = 20260913


def run(executable: str, pattern: str, subject: str, mode: str) -> int:
    result = subprocess.run(
        [executable, pattern, subject, mode], capture_output=True, text=True, timeout=5
    )
    if result.stderr or result.returncode < 0:
        raise AssertionError((pattern, subject, mode, result.returncode, result.stderr))
    return result.returncode


def expected_span(pattern: str, subject: str) -> int:
    # Independent oracle: enumerate substrings, using Python only for whole-string membership.
    expression = re.compile(pattern, re.ASCII)
    for begin in range(len(subject) + 1):
        for end in range(len(subject), begin - 1, -1):
            if expression.fullmatch(subject[begin:end]) is not None:
                first = len(subject[:begin].encode())
                last = len(subject[:end].encode())
                return first * 9 + last + 1
    return 0


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    arguments = parser.parse_args()
    rng = random.Random(SEED)
    atoms = ["a", "b", "é", ".", "[ab]", "[^a]", "[a-z]", r"\d", r"\w", ""]
    repetitions = ["", "*", "+", "?", "{0}", "{2}", "{1,3}", "{0,2}", "{2,}"]
    corpus = [("(a?)*", "aaa"), ("((ab){1,2}){1,2}", "ababab"), ("(a|ab)*", "abab")]
    for _ in range(350):
        left, right = rng.choice(atoms), rng.choice(atoms)
        pattern = "(" + left + "|" + right + ")" + rng.choice(repetitions)
        if rng.randrange(2):
            pattern += "(" + rng.choice(atoms) + ")" + rng.choice(repetitions)
        subject = "".join(rng.choice("ab1!é") for _ in range(rng.randrange(5)))
        corpus.append((pattern, subject))
    for pattern, subject in corpus:
        expected = expected_span(pattern, subject)
        actual = run(arguments.executable, pattern, subject, "s")
        assert actual == expected, (SEED, pattern, subject, expected, actual)
        expected = int(re.fullmatch(pattern, subject, re.ASCII) is not None)
        actual = run(arguments.executable, pattern, subject, "f")
        assert actual == expected, (SEED, pattern, subject, "full", expected, actual)
    # No oracle for invalid grammar: the contract here is bounded termination without a crash.
    for _ in range(350):
        pattern = "".join(rng.choice("ab()[]{}|*+?\\012,^$:-") for _ in range(rng.randrange(25)))
        actual = run(arguments.executable, pattern, "abab", "s")
        assert 0 <= actual <= 81 or actual == 106, (SEED, pattern, actual)
    print(f"regex: {len(corpus) * 2} differential checks and 350 malformed probes; seed={SEED}")


if __name__ == "__main__":
    main()
