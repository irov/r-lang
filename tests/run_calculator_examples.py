#!/usr/bin/env python3
"""Run the calculator as a user would, comparing every math command with numeric oracles."""
from __future__ import annotations
import argparse
import cmath
import json
import math
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from generate_calculator import SIGNATURE, TYPES


def scalar_case(operation: str, kind: str) -> tuple[list[str], list[float | bool]]:
    a = 0.5
    b = 1.25
    if operation == 'acosh': a = 2.0
    if operation in {'gamma', 'log_gamma'}: a = 3.0
    if operation in {'abs', 'sign_bit'}: a = -0.5
    if operation == 'compose_binary': return ['0.5', '4'], [8.0]
    if operation == 'split_binary': return [str(a)], list(math.frexp(a))
    if operation == 'split_fraction':
        fraction, whole = math.modf(a)
        return [str(a)], [whole, fraction]
    unary = {
        'abs': abs, 'round': lambda x: math.copysign(math.floor(abs(x) + 0.5), x),
        'is_finite': math.isfinite, 'is_infinite': math.isinf, 'is_nan': math.isnan,
        'is_normal': lambda x: x != 0.0 and math.isfinite(x),
        'sign_bit': lambda x: math.copysign(1.0, x) < 0.0,
        'log_gamma': math.lgamma,
    }
    binary = {'copy_sign': math.copysign, 'min': min, 'max': max,
              'atan2': math.atan2, 'pow': math.pow, 'hypot': math.hypot, 'remainder': math.remainder}
    if operation == 'next_after':
        # Adjacent values depend on the selected precision; validate direction and distance below.
        return [str(a), str(b)], [a]
    if operation in binary: return [str(a), str(b)], [binary[operation](a, b)]
    fn = unary.get(operation) or getattr(math, operation)
    return [str(a)], [fn(a)]


def complex_case(operation: str) -> tuple[list[str], list[float]]:
    a, b = complex(0.5, 0.25), complex(0.25, 0.125)
    binary = {'add': lambda x, y: x+y, 'sub': lambda x, y: x-y,
              'mul': lambda x, y: x*y, 'div': lambda x, y: x/y, 'pow': pow}
    args = ['0.5', '0.25']
    if operation in binary:
        args += ['0.25', '0.125']
        result = binary[operation](a, b)
    elif operation == 'conjugate': result = a.conjugate()
    elif operation == 'phase': return args, [cmath.phase(a)]
    elif operation == 'magnitude': return args, [abs(a)]
    else: result = getattr(cmath, operation)(a)
    return args, [result.real, result.imag]


def invoke(exe: str, args: list[str], status: int = 0) -> str:
    run = subprocess.run([exe, *args], capture_output=True, text=True, timeout=10)
    if run.returncode != status or run.stderr:
        raise AssertionError((args, run.returncode, run.stdout, run.stderr))
    return run.stdout.strip()


def check_expressions(exe: str) -> None:
    """The eval command: a recursive parser whose nesting @recursion(depth = 16) bounds (L35)."""
    assert invoke(exe, ['eval', '2 * (3 + 4)']) == '14'
    assert invoke(exe, ['eval', '-(1.5 - 4) / 2']) == '1.25'
    assert invoke(exe, ['eval', '1 +'], 65) == 'syntax error at byte 3'
    assert invoke(exe, ['eval', '(1'], 65) == 'syntax error at byte 2'
    assert invoke(exe, ['eval', '1', '2'], 64) == 'usage: calculator eval EXPRESSION'
    deepest = '(' * 15 + '1' + ')' * 15
    assert invoke(exe, ['eval', deepest]) == '1'
    refused = 'expression nests deeper than 16 levels'
    for text in ('(' * 16 + '1' + ')' * 16, '-' * 20 + '1', '(' * 5000 + '1' + ')' * 5000):
        assert invoke(exe, ['eval', text], 65) == refused, text[:8]
    # The rpn command: a state machine on a labeled switch (R-STMT-0024, L46).
    assert invoke(exe, ['rpn', '3 4 + 2 *']) == '14'
    assert invoke(exe, ['rpn', '  1.5 4 - 2 /']) == '-1.25'
    assert invoke(exe, ['rpn', '2 3 4 * +']) == '14'
    assert invoke(exe, ['rpn', ' '.join(['1'] * 16) + ' +' * 15]) == '16'
    assert invoke(exe, ['rpn', '1 +'], 65) == 'syntax error at byte 2'
    assert invoke(exe, ['rpn', '1 2'], 65) == 'syntax error at byte 3'
    assert invoke(exe, ['rpn', '1 x'], 65) == 'syntax error at byte 2'
    assert invoke(exe, ['rpn', ' '.join(['1'] * 17)], 65) == 'syntax error at byte 32'
    assert invoke(exe, ['rpn', '1', '2'], 64) == 'usage: calculator rpn EXPRESSION'


def check_stack_bound(ir: Path) -> None:
    """The emitter's measurement in the IR counts 16 frames of each parser function (R-FUNC-0004).

    r.stack.frames holds each function with its frame, @recursion depth and the bound below a
    call into it; r.stack.entries each entry with its bound.
    """
    text = ir.read_text(encoding='utf-8')
    nodes = {number: fields for number, fields in
             re.findall(r'^!(\d+) = !\{(!"[^"]*"(?:, i64 \d+)+)\}$', text, re.M)}

    def listed(name: str) -> list[tuple[str, list[int]]]:
        match = re.search(rf'^!{re.escape(name)} = !\{{([^}}]*)\}}$', text, re.M)
        assert match is not None, name
        rows = []
        for reference in match.group(1).split(', '):
            fields = nodes[reference.lstrip('!')]
            label = re.match(r'!"([^"]*)"', fields).group(1)
            rows.append((label, [int(value) for value in re.findall(r'i64 (\d+)', fields)]))
        return rows

    frames = listed('r.stack.frames')
    entries = [values[0] for _, values in listed('r.stack.entries')]
    recursive = [(name, values) for name, values in frames if values[1] != 0]
    assert len(recursive) == 3 and all(values[1] == 16 for _, values in recursive), recursive
    bounds = {values[2] for _, values in recursive}
    assert len(bounds) == 1, recursive
    bound = bounds.pop()
    sizes = [values[0] for _, values in recursive]
    assert bound >= 16 * sum(sizes) + max(sizes), (bound, sizes)
    assert max(entries) >= bound, (max(entries), bound)
    print(f'Calculator: parser recursion bounded at {bound} stack bytes (3 functions x 16)')


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', required=True)
    parser.add_argument('--stack-ir', type=Path)
    options = parser.parse_args()
    inventory = json.loads((ROOT / 'library/generated/api_inventory/implementation_inventory.json').read_text())
    count = 0
    for item in inventory['items']:
        if item['module'] != 'std.math' or item['item_kind'] not in {'operation', 'operation_specialization'} or item['id'] == 'std.math::as_error':
            continue
        match = SIGNATURE.fullmatch(item['source_signature'])
        assert match is not None
        name = match.group(1)
        kind = next(t for t in sorted(TYPES, key=len, reverse=True) if name.endswith('_' + t))
        operation = name[:-(len(kind) + 1)]
        args, expected = complex_case(operation) if kind.startswith('complex_') else scalar_case(operation, kind)
        text = invoke(options.executable, [operation, kind, *args])
        if isinstance(expected[0], bool):
            assert text == str(expected[0]).lower(), (name, text, expected)
        else:
            actual = [float(word) for word in text.split()]
            assert len(actual) == len(expected), (name, text, expected)
            tolerance = 2e-6 if kind in {'f32', 'c_float', 'complex_f32'} else 2e-12
            assert all(math.isclose(a, e, rel_tol=tolerance, abs_tol=tolerance) for a, e in zip(actual, expected)), (name, text, expected)
            if operation == 'next_after': assert actual[0] > 0.5, (name, text)
        count += 1
    for args, status in [(['sqrt','f64','-1'],65), (['log','f64','0'],65),
                         (['sqrt','f64','bad'],65), (['sqrt','f64'],64),
                         (['no_such_operation','f64','1'],64), (['sqrt','unknown','1'],64)]:
        assert invoke(options.executable,args,status), args
    assert 'calculator' in invoke(options.executable,[])
    check_expressions(options.executable)
    if options.stack_ir is not None:
        check_stack_bound(options.stack_ir)
    print(f'Calculator: {count} typed operations, six input/error paths, eval, rpn and help passed')


if __name__ == '__main__': main()
