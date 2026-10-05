#!/usr/bin/env python3
"""Check opaque public contracts, source ordering, and representation fingerprints."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-opaque-results-') as directory:
        root = Path(directory)
        provider = root / 'provider.r'
        consumer = root / 'consumer.r'
        definition = '''module hidden.provider;
trait Read { i32 read(const Self* this);
    i32 again(const Self* this) { return this->read(); }
};
protected struct Storage { i32 value; };
impl Read for Storage { i32 read(const Storage* this) { return this->value; } };
opaque(Read & copy) make(i32 value) { return Storage {.value=value}; }
@generic<T: copy> opaque(fn() -> T & copy) constant(T value) {
    fn T read() move(value) { return value; }
    return read;
}
'''
        use = '''module hidden.consumer;
import hidden.provider;
@generic<T: hidden.provider::Read> i32 read(const T* value) { return value->again(); }
i32 main() {
    auto value=hidden.provider::make(20);
    auto closure=hidden.provider::constant(22);
    return read(&value)+closure()-42;
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
        assert '(interface version=32 ' in interface
        assert 'return=(opaque module="hidden.provider"' in interface
        assert 'opaque_definition="' in interface
        make = next(line for line in interface.splitlines() if 'function name="hidden.provider::make"' in line)
        assert 'Storage' not in make.split(' parameters=')[0]
        assert 'name="Read" module="hidden.provider"' in make
        assert interface == emit('interface', [consumer, provider])
        assert emit('c17', [provider, consumer]) == emit('c17', [consumer, provider])
        provider.write_text(definition.replace('.value=value', '.value=value+1'))
        changed = emit('interface', [provider, consumer])
        assert re.findall(r'opaque_definition="([0-9a-f]+)"', changed) != re.findall(
            r'opaque_definition="([0-9a-f]+)"', interface)
        provider.write_text(definition)
        consumer.write_text(use.replace('return read(&value)+closure()-42;', 'return value.value;'))
        assert 'R-DIAG-' in emit('interface', [provider, consumer], False)


if __name__ == '__main__':
    main()
