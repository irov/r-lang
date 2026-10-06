#!/usr/bin/env python3
"""Check scoped async source imports, generic contracts, and deterministic metadata."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-scoped-contracts-') as directory:
        root = Path(directory)
        provider = root / 'provider.r'
        consumer = root / 'consumer.r'
        definition = '''module scopes.provider;
@scoped async i32 inspect(const i32* value) { return *value; }
@generic<T: copy & send & sync & unborrowed>
@scoped async T read(const T* value) { return *value; }
'''
        use = '''module scopes.consumer;
import scopes.provider;
async i32 main() {
    i32 first=20;
    i32 second=22;
    try {
        task_scope(2) group {
            auto a=scopes.provider::inspect(&first);
            auto b=scopes.provider::read(&second);
            await group.all();
            return await move a + await move b - 42;
        }
    } catch (std.async::start_error failure) { return 1; }
}
'''
        provider.write_text(definition)
        consumer.write_text(use)

        def emit(mode, paths, accepted=True):
            result = subprocess.run([args.front, '--emit=' + mode, *map(str, paths)],
                                    text=True, capture_output=True, timeout=30)
            assert result.returncode == (0 if accepted else 1), result.stderr
            return result.stdout if accepted else result.stderr

        interface = emit('interface', [provider, consumer])
        assert '(interface version=33 ' in interface
        functions = [line for line in interface.splitlines()
                     if 'name="scopes.provider::inspect"' in line or
                     'name="scopes.provider::read"' in line]
        assert len(functions) >= 2, functions
        assert all('scoped=true' in line for line in functions), functions
        assert interface == emit('interface', [consumer, provider])
        assert emit('c17', [provider, consumer]) == emit('c17', [consumer, provider])
        consumer.write_text(use.replace('task_scope(2) group {', '{').replace(
            '            await group.all();\n', ''))
        diagnostic = emit('interface', [provider, consumer], False)
        assert 'R-DIAG-' in diagnostic and 'scope' in diagnostic, diagnostic


if __name__ == '__main__':
    main()
