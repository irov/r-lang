#!/usr/bin/env python3
"""Check typed constant identities, imports and deterministic monomorphization."""
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
    with tempfile.TemporaryDirectory(prefix='r-const-generics-') as directory:
        root = Path(directory)
        api = root / 'api.r'
        app = root / 'app.r'
        api.write_text('''module constants.api;
const usize width = 4usize;
@generic<T, const usize N>
struct Buffer { T[N] data; };
@generic<T: copy, const usize N>
usize size(const Buffer<T, N>* input) { return N; }
''')
        app.write_text('''module constants.app;
import constants.api;
i32 main() {
    constants.api::Buffer<u8, constants.api::width> first = {};
    constants.api::Buffer<u8, 2usize + 2usize> second = first;
    constants.api::Buffer<u8, 0x4usize> third = second;
    constants.api::Buffer<u8, 8usize> large = {};
    usize a = constants.api::size(&first);
    usize b = constants.api::size(&second);
    usize c = constants.api::size(&third);
    usize d = constants.api::size(&large);
    i32 status = a + b + c + d == 20usize ? 0 : 1;
    return status;
}
''')

        def emit(mode, paths):
            result = subprocess.run([args.front, *emit_options(mode), *map(str, paths)],
                                    capture_output=True, text=True, timeout=30)
            assert result.returncode == 0, (result.returncode, result.stderr)
            return result.stdout

        interface = emit('interface', [api, app])
        assert interface == emit('interface', [app, api])
        assert interface.endswith(')\n') and '(interface version=34 ' in interface
        assert '(name="N" constant_type=usize)' in interface
        assert '(constant usize 4)' in interface and '(constant usize 8)' in interface
        closed = [line for line in interface.splitlines()
                  if '(function name="constants.api::size<' in line]
        assert len(closed) == 2, closed
        assert emit('llvm-ir', [api, app]) == emit('llvm-ir', [app, api])
        changed = api.read_text().replace('return N;', 'return N + 1usize;')
        api.write_text(changed)
        assert interface != emit('interface', [api, app])
        app.write_text("""module constants.growth;
@generic<const usize N> struct B { u8[N] data; };
@generic<const usize N> void grow(B<N> input) { B<N + 1usize> next = {}; grow(next); }
i32 main() { B<1usize> input = {}; grow(input); return 0; }
""")
        limited = subprocess.run([args.front, '--emit=hir', str(app)],
                                 capture_output=True, text=True, timeout=20)
        assert limited.returncode == 2 and 'R-DIAG-LIMIT-001' in limited.stderr
        assert 'grow<constant[usize,' in limited.stderr and '<-' in limited.stderr

        # R-TYPE-0047: a bound computed by a call over the parameters is written as its formula;
        # instances closed at module scope in another module get the same values in any order.
        api.write_text('''module constants.sizes;
@generic<T>
usize size_of() { return sizeof(T); }
@generic<T>
struct Cell { u8[size_of::<T>()] bytes; };
''')
        app.write_text('''module constants.frames;
import constants.sizes;
struct Frame { constants.sizes::Cell<u64> wide; constants.sizes::Cell<u16> narrow; };
i32 main() {
    Frame frame = {};
    i32 status = len(frame.wide.bytes) + len(frame.narrow.bytes) == 10usize ? 0 : 1;
    return status;
}
''')
        interface = emit('interface', [api, app])
        assert interface == emit('interface', [app, api])
        assert ('(constant call "constants.sizes:usize: size_of :: < $0 > ( )" (parameter "T"))'
                in interface)
        assert emit('llvm-ir', [api, app]) == emit('llvm-ir', [app, api])

        # R-TYPE-0047: typed constant parameters keep their type and value in interfaces.
        api.write_text('''module constants.typed;
@generic<const bool F, const i8 N>
struct Pair { u8 value; };
@generic<const u32 M>
u32 mask(u32 value) { return value & M; }
''')
        app.write_text('''module constants.users;
import constants.typed;
struct Holder { constants.typed::Pair<true, -3> pair; };
i32 main() {
    Holder holder = {};
    holder as void;
    i32 status = constants.typed::mask::<0x0Fu32>(0xABu32) == 0x0Bu32 ? 0 : 1;
    return status;
}
''')
        interface = emit('interface', [api, app])
        assert interface == emit('interface', [app, api])
        for text in ('constant_type=bool', 'constant_type=i8', 'constant_type=u32',
                     '(constant bool true)', '(constant i8 -3)', 'mask<constant[u32,15]>'):
            assert text in interface, text
        assert emit('llvm-ir', [api, app]) == emit('llvm-ir', [app, api])

        # R-TYPE-0050: a trait lists its associated constants; a bound over one is written as its
        # trait, name and owner, closed by an implementation in another module.
        api.write_text('''module constants.codec;
trait Encoded {
    const usize SIZE;
    const bool PACKED = false;
};
@generic<T: Encoded>
struct Frame { u8[T::SIZE] payload; };
''')
        app.write_text('''module constants.wire;
import constants.codec::{Encoded, Frame};
struct Flag { u8 value; };
impl Encoded for Flag { const usize SIZE = 3usize; const bool PACKED = true; };
struct Wire { Frame<Flag> flag; };
i32 main() {
    Wire message = {};
    i32 status = len(message.flag.payload) == Flag::SIZE ? 0 : 1;
    return status;
}
''')
        interface = emit('interface', [api, app])
        assert interface == emit('interface', [app, api])
        for text in ('constants=((name="SIZE" type=usize default=false) '
                     '(name="PACKED" type=bool default=true))',
                     '(constant associated "constants.codec" "Encoded" "SIZE" (parameter "T"))'):
            assert text in interface, text
        assert emit('llvm-ir', [api, app]) == emit('llvm-ir', [app, api])
        app.write_text(app.read_text().replace('SIZE = 3usize', 'SIZE = 4usize'))
        assert interface != emit('interface', [api, app])
    print('const generics: canonical values, source imports, cache, interface fingerprints, '
          'computed formulas, typed and associated constants passed')


if __name__ == '__main__':
    main()
