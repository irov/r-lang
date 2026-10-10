#!/usr/bin/env python3
"""B8.2: mutation testing of R source (tools/mutate_r.py).

The mutants of an R text are its comparisons, binary additions and subtractions, logical
connectives, boolean and integer literals, never generic brackets, signs, comments or strings;
a gcov report limits them to the lines that ran. Run against the functions of
codegen_mutation_target.r, every mutant is killed by the checks of its main except the two that
keep the meaning of clamp.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f'mutation tool check failed: {message}', file=sys.stderr)
        sys.exit(1)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tool', type=Path, required=True)
    parser.add_argument('--build', type=Path, required=True)
    parser.add_argument('--ctest', required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    args = parser.parse_args()

    spec = importlib.util.spec_from_file_location('mutate_r', args.tool)
    tool = importlib.util.module_from_spec(spec)
    sys.modules['mutate_r'] = tool
    spec.loader.exec_module(tool)
    sample = ('x = std.dict::create::<i32, i32>(); // a < b\n'
              'y = a - b + -3; s = "p == q"; c = \'<\';\n'
              'if (a <= b && flag == true) { n += 10usize; }\n')
    found = [(m.line, m.original, m.replacement) for m in tool.mutants(sample)]
    require(found == [(2, '-', '+'), (2, '+', '-'), (2, '3', '4'), (3, '<=', '<'), (3, '&&', '||'),
                      (3, '==', '!='), (3, 'true', 'false'), (3, '+=', '-='),
                      (3, '10usize', '11usize')],
            f'mutants of the sample: {found}')
    with tempfile.TemporaryDirectory(prefix='r-mutation-check-') as directory:
        report = Path(directory) / 'gcov.txt'
        report.write_text('        -:    1:x\n        4:    2:y\n    #####:    3:z\n',
                          encoding='utf-8')
        require(tool.covered_lines(report) == {2}, 'lines of a gcov report')

        lines = args.fixture.read_text(encoding='utf-8').splitlines()
        first = next(i for i, line in enumerate(lines, 1) if line.startswith('protected i32 clamp'))
        last = next(i for i, line in enumerate(lines, 1) if line.startswith('i32 main()')) - 1
        output = Path(directory) / 'report.json'
        result = subprocess.run(
            [sys.executable, str(args.tool), '--build', str(args.build), '--ctest', args.ctest,
             '--test', 'r_frontend_codegen_mutation_target', '--source', str(args.fixture),
             '--lines', f'{first}-{last}', '--jobs', '4', '--output', str(output)],
            capture_output=True, text=True, check=False)
        require(result.returncode == 0, result.stdout + result.stderr)
        data = json.loads(output.read_text(encoding='utf-8'))
        clamp_low = next(i for i, line in enumerate(lines, 1) if 'if (value < low)' in line)
        clamp_high = next(i for i, line in enumerate(lines, 1) if 'if (value > high)' in line)
        expected = [f'{args.fixture.name}:{clamp_low}:15: < -> <=',
                    f'{args.fixture.name}:{clamp_high}:15: > -> >=']
        require(data['mutants'] == 12 and data['killed'] == 10 and data['stillborn'] == 0 and
                data['survivors'] == expected, json.dumps(data))
    print('mutation tool checks passed')


if __name__ == '__main__':
    main()
