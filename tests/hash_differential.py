"""Deterministic differential test of the R part of std.hash and of std.random::generator (M20)
through executable R code: SHA-256 and SHA-512 over data in pieces and HMAC against hashlib and
hmac of the Python standard library, and the seeded generator against the reference of
tests/m20_reference.py."""

from __future__ import annotations

import argparse
import hashlib
import hmac
from pathlib import Path
import random
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from m20_reference import Generator  # noqa: E402

SEED = 20260929


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True)
    arguments = parser.parse_args()
    rng = random.Random(SEED)
    alphabet = "abcXYZ019 -_~!é€😀\n"
    cases: list[tuple[str, str, str, str]] = []
    for length in list(range(0, 140)) + [255, 256, 257, 1000]:
        text = "".join(rng.choice(alphabet) for _ in range(length))
        data = text.encode()
        cases.append(("sha256", "-", text, hashlib.sha256(data).hexdigest()))
        cases.append(("sha512", "-", text, hashlib.sha512(data).hexdigest()))
    for _ in range(120):
        key = "".join(rng.choice(alphabet) for _ in range(rng.randrange(0, 200)))
        text = "".join(rng.choice(alphabet) for _ in range(rng.randrange(0, 300)))
        cases.append(("hmac256", key, text,
                      hmac.new(key.encode(), text.encode(), hashlib.sha256).hexdigest()))
        cases.append(("hmac512", key, text,
                      hmac.new(key.encode(), text.encode(), hashlib.sha512).hexdigest()))
    for _ in range(60):
        seed = rng.randrange(0, 1 << 64)
        reference = Generator(seed)
        word = reference.next_u64()
        half = reference.next_u32()
        tenth = reference.below(10)
        ranged = reference.range(100, 200)
        noise = reference.fill(5).hex()
        cards = reference.shuffle(range(8))
        expected = " ".join(str(value) for value in [word, half, tenth, ranged, noise, *cards])
        cases.append(("generator", str(seed), "-", expected))
    command = [arguments.executable]
    for mode, key, text, _expected in cases:
        command.extend([mode, key, text])
    result = subprocess.run(command, capture_output=True, text=True, timeout=120)
    if result.returncode != 0 or result.stderr:
        raise AssertionError((result.returncode, result.stderr))
    lines = result.stdout.split("\n")
    assert lines[-1] == "" and len(lines) == len(cases) + 1, (len(lines), len(cases))
    for (mode, key, text, expected), actual in zip(cases, lines):
        assert actual == expected, (SEED, mode, key, text, expected, actual)
    print(f"hash and generator differential cases checked: {len(cases)}")


if __name__ == "__main__":
    main()
