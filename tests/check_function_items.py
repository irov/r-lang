#!/usr/bin/env python3
"""Verify cross-module callable identities and structural error-set contracts."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-function-items-') as directory:
        root = Path(directory)
        provider = root / 'provider.r'
        consumer = root / 'consumer.r'
        provider.write_text('''module items.provider;
error First { i32 code; };
error Second { i32 code; };
i32 increment(i32 value) { return value + 1; }
i32 checked(i32 value) throws First, Second {
    if (value == 0) { throw First {.code=1}; }
    if (value < 0) { throw Second {.code=2}; }
    return value;
}
@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
i32 invoke(F operation, i32 value) throws E { return operation(value); }
opaque(fn(i32) -> i32 & copy) factory() { return increment; }
''')
        consumer.write_text('''module items.consumer;
import items.provider;
i32 main() {
    auto operation = items.provider::increment;
    if (operation(41) != 42) { return 1; }
    if (items.provider::invoke(items.provider::increment, 41) != 42) { return 2; }
    auto hidden = items.provider::factory();
    if (hidden(41) != 42) { return 3; }
    try { return items.provider::invoke(items.provider::checked, 1) - 1; }
    catch (items.provider::First failure) { failure as void; return 4; }
    catch (items.provider::Second failure) { failure as void; return 5; }
}
''')

        def emit(mode, paths):
            result = subprocess.run([args.front, '--emit=' + mode, *map(str, paths)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (result.returncode, result.stderr)
            return result.stdout

        interface = emit('interface', [provider, consumer])
        assert interface == emit('interface', [consumer, provider])
        assert '(interface version=32 ' in interface
        assert '(name="E" constraints=(unborrowed errors))' in interface
        assert '$function(increment)' in interface and '$function(checked)' in interface
        assert '(callable mode=shared parameters=(i32) return=i32 throws=(effects ' in interface
        assert 'invalid' not in interface
        mir = emit('mir', [provider, consumer])
        assert 'call callee="items.provider::increment"' in mir
        assert 'call callee="items.provider::checked"' in mir
        assert 'indirect_call' not in mir
        assert emit('c17', [provider, consumer]) == emit('c17', [consumer, provider])

        provider.write_text(provider.read_text().replace('throws First, Second',
                                                        'throws Second, First'))
        reordered = emit('interface', [provider, consumer])
        before = [line for line in interface.splitlines() if '  (function name=' in line]
        after = [line for line in reordered.splitlines() if '  (function name=' in line]
        assert before == after

    print('function items: module order, direct calls, empty and exact error sets verified')


if __name__ == '__main__':
    main()
