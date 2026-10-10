#!/usr/bin/env python3
"""Check a library built by r-front (Core R-FFI-0064..0066, B10): the export list, the C header
(compiled as C17 and C++17), the shared library linked by r-front with its versions and exactly
its exports, a C program that uses it from several threads and one that opens it at run time, the
contract_violation panic of an export entered outside initialize..shutdown, the ABI baseline and
its verdicts, and the rejections of the library options and of APIs C cannot declare."""

import argparse
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import tempfile

EXPORTS = ['geometry_initialize', 'geometry_shutdown', 'geometry_segment_length_squared',
           'geometry_midpoint', 'geometry_scale', 'geometry_area', 'geometry_number_real',
           'geometry_flags_layer', 'geometry_calls', 'geometry_node_sum', 'geometry_block_length',
           'geometry_counter_new', 'geometry_counter_add', 'geometry_counter_free']

PROTOTYPES = [
    'double geometry_segment_length_squared(segment line);',
    'point geometry_midpoint(point left, point right);',
    'void geometry_scale(point *target, double factor);',
    'double geometry_area(shape kind, double size);',
    'double geometry_number_real(number value);',
    'unsigned int geometry_flags_layer(flags value);',
    'uint64_t geometry_calls(void);',
    'int geometry_node_sum(node *first, int length);',
    'unsigned int geometry_block_length(block value);',
    'void *geometry_counter_new(void);',
    'uint64_t geometry_counter_add(void *handle, uint64_t amount);',
    'void geometry_counter_free(void *handle);',
]

PANIC = 'R panic: contract_violation at module 0 bytes [0,0)\n'

# name -> (sources, extra options, exit status, stderr): libraries and options r-front refuses.
REJECTED = {
    'main': ({'main.r': '''module library.with_main;
i32 main() { return 0; }
'''}, [], 1, 'r-front: R-DIAG-FLOW-001 [R-FUNC-0008]: a library has no main; its exports are its '
       'entry points (R-FFI-0064)\n'),
    'conflict': ({'conflict.r': '''module library.conflict;
@export_name("probe_initialize")
@safety("PROBE", "none")
extern "C" c_int start() { return 0 as c_int; }
'''}, [], 1, 'r-front: R-DIAG-FFI-003 [R-FFI-0064]: an export of the program is named '
       'probe_initialize, which the library defines itself\n'),
    'twice': ({'first.r': '''module library.first;
@repr(C)
struct point { c_int x; };
@export_name("probe_first")
@safety("PROBE", "none")
extern "C" c_int first(point value) { return value.x; }
''', 'second.r': '''module library.second;
@repr(C)
struct point { c_long y; };
@export_name("probe_second")
@safety("PROBE", "none")
extern "C" c_long second(point value) { return value.y; }
'''}, ['--emit=c-header'], 1, 'r-front: R-DIAG-FFI-003 [R-FFI-0065]: the C header of the library '
       'would declare point twice\n'),
    'keyword': ({'keyword.r': '''module library.keyword;
@repr(C)
struct template { c_int x; };
@export_name("probe_keyword")
@safety("PROBE", "none")
extern "C" c_int keyword(template value) { return value.x; }
'''}, ['--emit=abi-baseline'], 1, 'r-front: R-DIAG-FFI-003 [R-FFI-0065]: the C header of the '
       'library would declare template, a keyword of C or C++ or a name the C implementation '
       'reserves\n'),
    'member': ({'member.r': '''module library.member;
@repr(C)
struct box { c_int class; };
@export_name("probe_member")
@safety("PROBE", "none")
extern "C" c_int member(box value) { return value.class; }
'''}, ['--emit=c-header'], 1, 'r-front: R-DIAG-FFI-003 [R-FFI-0065]: a member of a type of the '
       'library API is named class, a keyword of C or C++\n'),
    'macro': ({'macro.r': '''module library.macro;
@repr(C)
enum mode : c_int { fast = 1 };
@export_name("mode_fast")
@safety("PROBE", "none")
extern "C" c_int fast(mode value) { return 1 as c_int; }
'''}, ['--emit=c-header'], 1, 'r-front: R-DIAG-FFI-003 [R-FFI-0065]: the C header of the library '
       'would declare mode_fast twice\n'),
}

# R-FFI-0065: an opaque type of an extern block comes from the headers of the block (B10-2).
HANDLES = '''module library.handles;

@header("probe_library.h")
extern "C" {
    @c_type(name = "probe_session", kind = "typedef")
    opaque struct Session;
}

@export_name("probe_session_pass")
@safety("PROBE", "none")
extern "C" raw Session* pass(raw Session* session) { return session; }
'''

OPTION_ERRORS = [
    (['--library', '_probe'], 'a library name is an ASCII C identifier'),
    (['--library', 'int'], 'a library name is an ASCII C identifier'),
    (['--library', '9probe'], 'a library name is an ASCII C identifier'),
    (['--library', 'probe', '--library-version', '1.2.3.4'], 'a library version is X[.Y[.Z]]'),
    (['--library', 'probe', '--compatibility-version', '1.256'], 'a library version is X[.Y[.Z]]'),
    (['--library', 'probe', '--library-version', '65536'], 'a library version is X[.Y[.Z]]'),
    (['--emit=c-header'], '--library is required for selected emit'),
    (['--emit=llvm-ir', '--abi-baseline', 'baseline.json'], '--library is required'),
    (['--library', 'probe', '--emit=llvm-ir', '--abi-baseline', 'baseline.json'],
     '--abi-baseline requires a library emit'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', required=True)
    parser.add_argument('--target-manifest', required=True)
    parser.add_argument('--linker', required=True)
    parser.add_argument('--source', required=True)
    parser.add_argument('--host', required=True)
    parser.add_argument('--c-link-manifest', required=True)
    parser.add_argument('--c-header-dir', required=True)
    parser.add_argument('--sanitize', choices=['none', 'address', 'thread'], default='none')
    parser.add_argument('--link-input', action='append', default=[])
    args = parser.parse_args()
    sysroot = subprocess.run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], capture_output=True,
                             text=True, check=True).stdout.strip()
    tools = Path(args.linker).parent
    sanitize = [] if args.sanitize == 'none' else [f'--sanitize={args.sanitize}']
    host_flags = {'none': [], 'address': ['-fsanitize=address,undefined', '-fno-omit-frame-pointer'],
                  'thread': ['-fsanitize=thread', '-fno-omit-frame-pointer']}[args.sanitize]
    # The C parts of the sanitized presets carry the undefined-behaviour checks as well; their
    # runtime is a further input of the link.
    link_extra = ['-fsanitize=undefined'] if args.sanitize == 'address' else []
    environment = dict(os.environ)
    environment.update({'ASAN_OPTIONS': 'abort_on_error=1', 'UBSAN_OPTIONS': 'halt_on_error=1',
                        'TSAN_OPTIONS': 'halt_on_error=1'})

    def front(options, sources, check=True):
        result = subprocess.run(
            [args.front, '--library-map', args.library_map, '--target-manifest',
             args.target_manifest, *options, *map(str, sources)],
            capture_output=True, text=True, timeout=600)
        if check:
            assert result.returncode == 0, (options, result.returncode, result.stderr)
        return result

    library = ['--library', 'geometry']
    source = [args.source]
    with tempfile.TemporaryDirectory(prefix='r-shared-library-') as directory:
        root = Path(directory)

        # R-FFI-0064: the exported symbols, NAME_initialize and NAME_shutdown first.
        listed = front(['--emit=export-list', *library], source).stdout.split()
        assert listed == ['_' + name for name in EXPORTS], listed

        # R-FFI-0065: the header, proved by libclang before it is written.
        header = front(['--emit=c-header', *library, '--library-version', '1.2.0', '--sysroot',
                        sysroot], source).stdout
        (root / 'geometry.h').write_text(header)
        for prototype in PROTOTYPES:
            assert prototype in header, (prototype, header)
        assert '#define GEOMETRY_VERSION "1.2.0"' in header, header
        assert '#define shape_square ((shape)2)' in header, header
        assert header.index('struct point {') < header.index('struct segment {'), header
        assert 'GEOMETRY_STATIC_ASSERT(offsetof(block, mark) == 8U, "offset of block.mark");' \
            in header, header
        assert 'struct __attribute__((packed)) header {' in header, header
        cxx = root / 'header.cpp'
        cxx.write_text('#include "geometry.h"\n')
        subprocess.run([str(tools / 'clang++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
                        '-pedantic', '-isysroot', sysroot, '-I', str(root), '-fsyntax-only',
                        str(cxx)], check=True, timeout=120)

        # R-FFI-0064, R-CMAP-0031: the shared library, linked and checked by r-front.
        output = root / 'libgeometry.dylib'
        inputs = [option for item in args.link_input + link_extra
                  for option in ('--link-input', item)]
        front(['--emit=shared-library', *library, '--library-version', '1.2.0', '--sysroot',
               sysroot, '--linker', args.linker, '--output', str(output), *sanitize, *inputs],
              source)
        names = subprocess.run([str(tools / 'llvm-nm'), '--extern-only', '--defined-only',
                                '--just-symbol-name', str(output)], capture_output=True,
                               text=True, check=True).stdout.split()
        assert sorted(names) == sorted('_' + name for name in EXPORTS), names
        identity = subprocess.run([str(tools / 'llvm-otool'), '-L', str(output)],
                                  capture_output=True, text=True, check=True).stdout
        assert '@rpath/libgeometry.dylib (compatibility version 1.0.0, current version 1.2.0)' \
            in identity, identity

        host = root / 'host'
        subprocess.run([args.linker, '-std=c17', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        *host_flags, '-isysroot', sysroot, '-I', str(root), args.host, '-L',
                        str(root), '-lgeometry', f'-Wl,-rpath,{root}', '-o', str(host)],
                       check=True, timeout=120)
        result = subprocess.run([str(host)], capture_output=True, text=True, timeout=120,
                                env=environment)
        assert (result.returncode, result.stdout, result.stderr) == (0, 'ok 1.2.0\n', ''), result
        # R-FFI-0064: an export entered before initialize or after shutdown.
        for mode in ('early', 'late'):
            result = subprocess.run([str(host), mode], capture_output=True, text=True,
                                    timeout=120, env=environment)
            assert result.returncode == -signal.SIGABRT, (mode, result)
            assert result.stderr.startswith(PANIC), (mode, result.stderr)

        opened = root / 'opened'
        subprocess.run([args.linker, '-std=c17', '-Wall', '-Wextra', '-Werror', '-pedantic',
                        '-DR_GEOMETRY_DLOPEN', *host_flags, '-isysroot', sysroot, '-I',
                        str(root), args.host, '-o', str(opened)], check=True, timeout=120)
        result = subprocess.run([str(opened), str(output)], capture_output=True, text=True,
                                timeout=120, env=environment)
        assert (result.returncode, result.stdout) == (0, 'dlopen ok\n'), result

        # R-FFI-0066: the baseline and the verdicts on it.
        text = front(['--emit=abi-baseline', *library, '--library-version', '1.2.0'],
                     source).stdout
        baseline = json.loads(text)
        assert baseline['schema'] == 'r-abi-baseline-0.1', baseline
        assert (baseline['library'], baseline['target'], baseline['version'],
                baseline['compatibility_version']) == ('geometry', 'arm64-apple-darwin', '1.2.0',
                                                       '1.0.0'), baseline
        assert [entry['name'] for entry in baseline['exports']] == EXPORTS[2:], baseline
        kinds = {entry['name']: entry['kind'] for entry in baseline['types']}
        assert kinds == {'point': 'struct', 'segment': 'struct', 'number': 'union',
                         'shape': 'enum', 'flags': 'struct', 'header': 'struct',
                         'block': 'struct', 'node': 'struct'}, kinds
        layer = [member for entry in baseline['types'] if entry['name'] == 'flags'
                 for member in entry['members'] if member['name'] == 'layer'][0]
        assert (layer['offset'], layer['bit_offset'], layer['bit_width']) == (0, 1, 7), layer

        def variant(name, change):
            document = json.loads(text)
            change(document)
            path = root / f'{name}.json'
            path.write_text(json.dumps(document, indent=2))
            return path

        def moved(document):
            [entry for entry in document['types'] if entry['name'] == 'point'][0]['members'][1][
                'offset'] = 4

        def renamed(document):
            document['exports'][0]['name'] = 'geometry_length'

        def retyped(document):
            document['exports'][1]['prototype'] = 'point geometry_midpoint(point)'

        def newer(document):
            document['compatibility_version'] = '1.1'

        def foreign(document):
            document['library'] = 'other'

        cases = [
            (root / 'same.json', [], 0, ''),
            (variant('moved', moved), [], 1, 'the type point of the ABI baseline {} changed at '
             'compatibility version 1.0.0'),
            (variant('moved', moved), ['--compatibility-version', '2'], 0, ''),
            (variant('renamed', renamed), [], 1, 'the export geometry_length of the ABI baseline '
             '{} is gone at compatibility version 1.0.0'),
            (variant('retyped', retyped), [], 1, 'the export geometry_midpoint of the ABI '
             'baseline {} changed at compatibility version 1.0.0'),
            (variant('newer', newer), [], 1, 'the compatibility version 1.0.0 is lower than that '
             'of the ABI baseline {}'),
            (variant('foreign', foreign), [], 1, '{} is no ABI baseline of the library geometry '
             'for this target'),
        ]
        (root / 'same.json').write_text(text)
        for path, options, status, message in cases:
            result = front(['--emit=export-list', *library, '--abi-baseline', str(path),
                            *options], source, check=False)
            assert result.returncode == status, (path.name, options, result.stderr)
            if status != 0:
                assert result.stderr.startswith('r-front: R-DIAG-FFI-004 [R-FFI-0066]: ' +
                                                message.format(path)), (path.name, result.stderr)

        for name, (files, options, status, message) in REJECTED.items():
            paths = []
            for file_name, content in files.items():
                path = root / name / file_name
                path.parent.mkdir(exist_ok=True)
                path.write_text(content)
                paths.append(path)
            emit = [] if any(o.startswith('--emit=') for o in options) else ['--emit=export-list']
            prefix = 'probe' if name != 'macro' else 'mode'
            result = front([*emit, *options, '--library', prefix, '--sysroot', sysroot], paths,
                           check=False)
            assert (result.returncode, result.stderr) == (status, message), (name, result)

        handles = root / 'handles.r'
        handles.write_text(HANDLES)
        header = front(['--emit=c-header', '--library', 'probe', '--sysroot', sysroot,
                        '--link-manifest', args.c_link_manifest, '--abi-header-dir',
                        args.c_header_dir], [handles]).stdout
        assert '#include <probe_library.h>\n' in header, header
        assert 'probe_session *probe_session_pass(probe_session *session);' in header, header

        for options, message in OPTION_ERRORS:
            result = front(options, source, check=False)
            assert result.returncode == 2, (options, result.returncode, result.stderr)
            assert message in result.stderr, (options, result.stderr)
    print('shared library checks passed')


if __name__ == '__main__':
    main()
