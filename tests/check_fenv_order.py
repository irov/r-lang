#!/usr/bin/env python3
"""B8-3: floating-point operations of R stay inside the default environment of R.

A function that switches the floating-point environment at the boundary with C is strictfp and
its floating-point operations are constrained intrinsics, which the optimizer keeps in order with
the switches. In the optimized IR of audit_std_c_raw_fn.r the addition of the callback add_values,
inlined into its export r_audit_rounding, lies between r_runtime_c_entry_begin and
r_runtime_c_entry_end, and the addition of invoke_external follows r_runtime_c_call_end; no plain
floating-point instruction is left in either function. The same holds with the thread sanitizer,
whose calls first exposed the reordering.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import subprocess
import sys


def require(condition: bool, message: str) -> None:
    if not condition:
        print(f'fenv order check failed: {message}', file=sys.stderr)
        sys.exit(1)


def function(ir: str, name: str) -> tuple[str, str]:
    match = re.search(r'^define [^\n]*@"?' + re.escape(name) + r'"?\(([^\n]*)\{\n(.*?)^\}', ir,
                      re.MULTILINE | re.DOTALL)
    require(match is not None, f'no function {name}')
    return match.group(0).split('\n', 1)[0], match.group(2)


def strict(ir: str, header: str) -> bool:
    groups = re.findall(r'#(\d+)', header.split(')')[-1])
    return any(re.search(rf'^attributes #{group} = \{{[^\n]*\bstrictfp\b', ir, re.MULTILINE)
               for group in groups)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', type=Path, required=True)
    parser.add_argument('--fixture', type=Path, required=True)
    args = parser.parse_args()
    for extra in ([], ['--sanitize=thread']):
        result = subprocess.run(
            [str(args.front), '--profile=hosted-native-async', '--emit=llvm-ir', *extra,
             str(args.fixture)], capture_output=True, text=True, check=False)
        require(result.returncode == 0, result.stderr[:400])
        ir = result.stdout
        for name, first, second in (('r_audit_rounding', 'r_runtime_c_entry_begin',
                                     'r_runtime_c_entry_end'),
                                    ('audit.std_c_raw_fn::invoke_external',
                                     'r_runtime_c_call_end', None)):
            header, body = function(ir, name)
            require(strict(ir, header), f'{name} is not strictfp {extra}')
            require(re.search(r'= f(add|sub|mul|div|rem|cmp) ', body) is None,
                    f'{name} keeps a plain floating-point instruction {extra}')
            add = body.find('@llvm.experimental.constrained.fadd')
            require(add != -1, f'{name} has no constrained addition {extra}')
            require(body.find(f'@{first}(') < add, f'{name}: the addition precedes {first}')
            if second is not None:
                end = body.find(f'@{second}(')
                require(end != -1 and add < end, f'{name}: the addition follows {second} {extra}')
    print('fenv order checks passed')


if __name__ == '__main__':
    main()
