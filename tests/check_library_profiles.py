#!/usr/bin/env python3
"""Check the profiles of the standard modules written in R (Library R-SLIB-RSRC-0002, M19-2):
a program that imports a module of the library map translates in the profile the map names and
in every higher one, and a lower profile requires R-DIAG-PROFILE-001."""

import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import tempfile

PROFILES = ('freestanding', 'allocation', 'hosted', 'hosted-thread', 'hosted-native-async')


def entries(library_map: Path) -> list[tuple[str, str]]:
    result = []
    for raw in library_map.read_text(encoding='utf-8').splitlines():
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        module, rest = (part.strip() for part in line.split('=', 1))
        parts = rest.split()
        result.append((module, parts[1] if len(parts) == 2 else 'freestanding'))
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', required=True)
    args = parser.parse_args()
    modules = entries(Path(args.library_map))
    assert modules, 'the library map lists no module'
    with tempfile.TemporaryDirectory(prefix='r-library-profiles-') as directory:
        root = Path(directory)

        def probe(case: tuple[str, str, str]) -> tuple[str, str, str, int, str]:
            module, least, profile = case
            path = root / f'{module.replace(".", "_")}_{profile.replace("-", "_")}.r'
            path.write_text(f'module profiles.probe;\nimport {module};\ni32 main() {{ return 0; }}\n',
                            encoding='utf-8')
            result = subprocess.run(
                [args.front, '--emit=mir', '--profile', profile, '--library-map',
                 args.library_map, str(path)],
                capture_output=True, text=True, timeout=120)
            return module, least, profile, result.returncode, result.stderr

        cases = [(module, least, profile) for module, least in modules for profile in PROFILES]
        with ThreadPoolExecutor(max_workers=8) as executor:
            outcomes = list(executor.map(probe, cases))
    failures = []
    for module, least, profile, code, stderr in outcomes:
        if PROFILES.index(profile) >= PROFILES.index(least):
            if code != 0:
                failures.append(f'{module} in {profile} (least {least}): {stderr.strip()[:300]}')
        elif code != 1 or 'R-DIAG-PROFILE-001' not in stderr:
            failures.append(f'{module} in {profile} below {least} is not a profile error: '
                            f'{code} {stderr.strip()[:300]}')
    assert not failures, '\n'.join(failures)
    print(f'library profile checks passed: {len(modules)} modules, {len(outcomes)} programs')


if __name__ == '__main__':
    main()
