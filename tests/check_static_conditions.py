#!/usr/bin/env python3
"""Check static selection across modules, profiles, interfaces and link plans."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def emit_options(mode):
    """Options of one output; LLVM IR lowers every function, not only those main reaches, so
    that code generation also accepts the functions main never calls."""
    return ['--emit=' + mode, '--all-functions'] if mode == 'llvm-ir' else ['--emit=' + mode]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-static-selection-') as directory:
        root = Path(directory)
        provider, consumer = root / 'provider.r', root / 'consumer.r'
        provider.write_text('''module selection.provider;
@generic<T> struct Box { T value; };
@generic<T>
T transfer(T value) {
    @if (T is copy) { return value; } @else { return move value; }
}
@if (core::profile is hosted-native-async) {
    error Selected { i32 value; };
    i32 platform() { return 42; }
} @else {
    error Selected { u32 value; };
    i32 platform() { return 7; }
}
@if (core::profile is imaginary_typo) { i32 ignored() { return 0; } }
''')
        # Unknown configuration names never silently select false.
        result = subprocess.run([args.front, '--emit=hir', str(provider)],
                                capture_output=True, text=True, timeout=30)
        assert result.returncode == 1 and 'R-DIAG-META-001' in result.stderr
        provider.write_text(provider.read_text().split('@if (core::profile is imaginary_typo)')[0])
        consumer.write_text('''module selection.consumer;
import selection.provider;
i32 main() {
    selection.provider::Box<i32> original = {.value = 42};
    selection.provider::Box<i32> copied = selection.provider::transfer(original);
    i32 platform = selection.provider::platform();
    return copied.value + platform - 84;
}
''')

        def emit(mode, paths, extra=()):
            entry = ['--entry', 'selection.consumer' if len(paths) == 2 else 'selection.hash_only'] if mode == 'link-plan' else []
            result = subprocess.run([args.front, *emit_options(mode), *entry, *extra,
                                     *map(str, paths)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, result.stderr or f'frontend status {result.returncode}'
            return result.stdout

        for mode in ('interface', 'llvm-ir', 'link-plan'):
            assert emit(mode, [provider, consumer]) == emit(mode, [consumer, provider]), mode
        for profile, literal in [('freestanding', 7), ('allocation', 7), ('hosted', 7),
                                 ('hosted-thread', 7), ('hosted-native-async', 42)]:
            hir = emit('hir', [provider], ('--profile', profile))
            assert f'value={literal})' in hir
            assert hir.count('name="platform"') == 1

        # Inactive declarations do not create names, FFI dependencies or attribute obligations.
        provider.write_text('''module selection.hash_only;
@if (core::target is "not-a-real-target") {
    @abi("c17") extern "C" { c_int absent_external(c_int x); }
    @noalloc i32 invalid_attribute = 0;
    struct Hidden { Unknown field; };
    i32 duplicate() { return undefined; }
    i32 duplicate() { return undefined; }
}
i32 main() {
    u8[3] source = {0x61u8, 0x62u8, 0x63u8};
    u32 checksum = std.hash::crc32(source);
    i32 status = checksum == 0x352441c2u32 ? 0 : 1;
    return status;
}
''')
        link = emit('link-plan', [provider])
        assert 'r_std_hash' in link and 'r_std_bytes' not in link and 'absent_external' not in link
        interface = emit('interface', [provider])
        assert 'Hidden' not in interface and 'duplicate' not in interface
        assert 'absent_external' not in emit('llvm-ir', [provider])

        # An inactive generic local must not close a constrained type with a rejected argument.
        provider.write_text('''module selection.closed_storage;
@generic<T: copy> struct CopyBox { T value; };
@generic<T> T transfer(T value) {
    @if (T is copy) {
        CopyBox<T> temporary = {.value = value};
        return temporary.value;
    } @else { return move value; }
}
i32 main() {
    own i32* value = new i32(42);
    own i32* result = transfer(move value);
    return *result - 42;
}
''')
        emit('llvm-ir', [provider])

        # R-META-0002: constant conditions select declarations of one module from the values
        # of another, independently of source order and together with profile predicates.
        config, queue, app = root / 'config.r', root / 'queue.r', root / 'app.r'
        config.write_text('''module selection.config;
usize scaled(usize value) { return value * 4usize; }
const usize SLOTS = scaled(16usize);
''')
        queue.write_text('''module selection.queue;
import selection.config;
@if (selection.config::SLOTS >= 64usize && core::profile is hosted-native-async) {
    struct Queue { u32[selection.config::SLOTS] slots; };
} @else @if (selection.config::SLOTS in 1usize..64usize) {
    struct Queue { Unknown slots; };
} @else {
    struct Queue { u32[8] slots; };
}
''')
        app.write_text('''module selection.app;
import selection.queue;
i32 main() {
    selection.queue::Queue queue = {};
    i32 status = len(queue.slots) == 64usize ? 0 : 1;
    return status;
}
''')

        def emit_values(mode, paths, extra=()):
            entry = ['--entry', 'selection.app'] if mode == 'link-plan' else []
            result = subprocess.run([args.front, *emit_options(mode), *entry, *extra,
                                     *map(str, paths)],
                                    capture_output=True, text=True, timeout=60)
            assert result.returncode == 0, result.stderr or f'frontend status {result.returncode}'
            return result.stdout

        for mode in ('interface', 'llvm-ir', 'link-plan'):
            assert (emit_values(mode, [config, queue, app]) ==
                    emit_values(mode, [app, queue, config])), mode
        # The selection depends on function bodies of another module: the interface records it.
        assert 'source-dependency module="selection.config"' in emit_values(
            'interface', [config, queue, app])
        assert '(fixed_array u32 64)' in emit_values('hir', [config, queue, app])
        hosted = emit_values('hir', [config, queue, app], ('--profile', 'hosted'))
        assert '(fixed_array u32 8)' in hosted and '(fixed_array u32 64)' not in hosted
        # A module condition that cannot be evaluated is reported with its own diagnostic.
        queue.write_text(queue.read_text().replace('SLOTS >= 64usize &&', 'missing() == 1 &&'))
        result = subprocess.run([args.front, '--emit=hir', str(config), str(queue), str(app)],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode == 1 and 'R-DIAG-NAME-001' in result.stderr, result.stderr
    print('static conditions: module order, profiles, inactive declarations, storage and '
          'constant conditions passed')


if __name__ == '__main__':
    main()
