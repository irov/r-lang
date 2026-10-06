#!/usr/bin/env python3
"""Verify resource contracts across module boundaries and interface fingerprints."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-resource-contracts-') as directory:
        root = Path(directory)
        provider = root / 'provider.r'
        consumer = root / 'consumer.r'
        provider_text = '''module resource.provider;
@noalloc @nonblocking
i32 double_value(i32 value) { return value * 2; }
@generic<T: copy>
@noalloc @nonblocking
T identity(T value) { return move value; }
'''
        provider.write_text(provider_text)
        consumer.write_text('''module resource.consumer;
import resource.provider;
@noalloc @nonblocking
i32 main() {
    i32 first = resource.provider::identity(21);
    i32 result = resource.provider::double_value(first);
    return result - 42;
}
''')

        def emit(mode, paths, valid=True, extra=()):
            result = subprocess.run([args.front, '--emit=' + mode, *extra, *map(str, paths)],
                                    text=True, capture_output=True, timeout=30)
            if valid and result.returncode != 0:
                raise AssertionError(result.stderr or f'frontend failed: {result.returncode}')
            if not valid and result.returncode != 1:
                raise AssertionError(f'expected diagnostic failure: {result.returncode}')
            return result.stdout if valid else result.stderr

        interface = emit('interface', [provider, consumer])
        assert interface == emit('interface', [consumer, provider])
        assert '(interface version=33 ' in interface
        assert interface.count('noalloc=true nonblocking=true') >= 3
        assert '(function-schema ' in interface
        assert emit('c17', [provider, consumer]) == emit('c17', [consumer, provider])

        # Removing a declared promise changes metadata even when body behavior is identical.
        provider.write_text(provider_text.replace('@noalloc @nonblocking', ''))
        unannotated = emit('interface', [provider, consumer])
        assert interface != unannotated
        assert 'noalloc=false nonblocking=false' in unannotated

        # A caller must inspect an unannotated imported R body instead of trusting its signature.
        provider.write_text(provider_text.replace('return value * 2;',
                                                 'own i32* scratch = new i32(0); return value * 2;')
                            .replace('@noalloc @nonblocking', ''))
        failure = emit('hir', [consumer, provider], valid=False)
        assert 'R-DIAG-RESOURCE-001' in failure
        assert 'main -> double_value -> new allocates heap storage' in failure

        # C declarations expose their trusted boundary promises, including when called directly.
        provider.write_text('''module resource.ffi;
@abi("c17") extern "C" {
    @safety("RESOURCE", "The C implementation meets both resource contracts")
    @noalloc @nonblocking c_int probe_increment(c_int value);
}
@noalloc @nonblocking
c_int invoke(c_int value) {
    unsafe { c_int result = probe_increment(value); return result; }
}
''')
        manifest = Path(__file__).parent / 'fixtures' / 'ffi_implicit_link_manifest.json'
        ffi = emit('interface', [provider], extra=('--link-manifest', str(manifest)))
        assert ffi.count('noalloc=true nonblocking=true') >= 2
        # Resource-qualified callable signatures survive module boundaries and erasure.
        provider.write_text('''module resource.provider;
@generic<F: fn @noalloc @nonblocking(i32) -> i32>
@noalloc @nonblocking
i32 dispatch(F operation) { i32 result = operation.call(41); return result; }
@noalloc void through(raw fn @noalloc() -> void operation) { unsafe { operation(); } }
''')
        consumer.write_text('''module resource.consumer;
import resource.provider;
i32 main() {
    fn @nonblocking @noalloc i32 increment(i32 value) { return value + 1; }
    i32 result = resource.provider::dispatch(increment);
    return result - 42;
}
''')
        qualified = emit('interface', [provider, consumer])
        assert qualified == emit('interface', [consumer, provider])
        assert '(callable mode=shared parameters=(i32) return=i32 noalloc=true nonblocking=true)' in qualified
        assert '(raw_fn parameters=() return=void noalloc=true)' in qualified
        assert emit('c17', [provider, consumer]) == emit('c17', [consumer, provider])
        consumer.write_text(consumer.read_text().replace('fn @nonblocking @noalloc', 'fn'))
        assert 'R-DIAG-TYPE-001' in emit('hir', [provider, consumer], valid=False)
    print('resource contracts: module order, body proof, FFI and interface metadata passed')


if __name__ == '__main__':
    main()
