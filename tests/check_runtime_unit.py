#!/usr/bin/env python3
"""Check units of the runtime written in R (Core R-FFI-0068, R-FFI-0046, R-FFI-0054, B12): a unit
exports functions and objects under the names of the runtime without an entry, stack preflight or
attachment, and keeps its protected callbacks internal; outside a unit an `extern "C"` object, a
runtime name, a protected callback and the unit's options are rejected, each diagnostic with its
exact position, code, rule and message."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

UNIT = '''
@safety("R-RUNTIME-PROBE-COUNTER", "read with atomic loads")
extern "C" atomic c_uint32 r_runtime_probe_counter = 0u32 as c_uint32;

@safety("R-RUNTIME-PROBE-LIMIT", "None")
extern "C" c_uint64 r_runtime_probe_limit = 64u64 as c_uint64;

@safety("R-RUNTIME-PROBE-BUMP", "None")
extern "C" c_uint32 r_runtime_probe_bump() {
    return core::atomic_fetch_add(&r_runtime_probe_counter, 1u32 as c_uint32,
                                  core::memory_order::relaxed);
}

@callback
@safety("R-RUNTIME-PROBE-WORK", "context addresses a c_uint32")
protected extern "C" void probe_work(raw void* context) {
    unsafe {
        raw c_uint32* count = context as raw c_uint32*;
        *count = ((*count as u32) + 1u32) as c_uint32;
    }
}

@safety("R-RUNTIME-PROBE-WORK-FUNCTION", "None")
extern "C" raw fn(raw void*) -> void r_runtime_probe_work_function() {
    return probe_work;
}
'''

# An entry whose frame alone is above the call transition of the target (16384 bytes).
DEEP_UNIT = '''
@safety("R-RUNTIME-PROBE-DEEP", "None")
extern "C" c_uint8 r_runtime_probe_deep(c_size index) {
    u8[32768] bytes = {};
    usize at = (index as usize) % 32768usize;
    bytes[at] = 1u8;
    return bytes[(at * 7usize) % 32768usize] as c_uint8;
}
'''

OBJECT_REQUIREMENTS = ('an exported object of a runtime unit has @safety, a C ABI or atomic C '
                       'type, a name of the runtime and no thread_local')

# name -> (unit?, source, [(line, column, code, rule, message)]); line 1 is the module line.
REJECTED = {
    'object-outside-unit': (False, '''
@safety("PROBE", "None")
extern "C" c_int exported_count = 1i32 as c_int;
i32 main() { return 0; }
''', [(3, 18, 'R-DIAG-FFI-003', 'R-FFI-0046',
       'an extern "C" object is exported only by a runtime unit')]),
    'runtime-name-outside-unit': (False, '''
@safety("PROBE", "None")
extern "C" void r_runtime_probe_thing() {
}
i32 main() { return 0; }
''', [(3, 12, 'R-DIAG-FFI-003', 'R-FFI-0054',
       'C export requires a public definition, @safety, and a non-reserved C name')]),
    'object-requirements': (True, '''
extern "C" c_uint32 r_runtime_probe_unsafe = 1u32 as c_uint32;
@safety("R-RUNTIME-PROBE", "None")
extern "C" u32 r_runtime_probe_fixed = 1u32;
@safety("R-RUNTIME-PROBE", "None")
extern "C" thread_local c_uint32 r_runtime_probe_local = 1u32 as c_uint32;
''', [(2, 21, 'R-DIAG-FFI-003', 'R-FFI-0068', OBJECT_REQUIREMENTS),
      (4, 16, 'R-DIAG-FFI-003', 'R-FFI-0068', OBJECT_REQUIREMENTS),
      (6, 34, 'R-DIAG-FFI-003', 'R-FFI-0068', OBJECT_REQUIREMENTS)]),
    # Only a runtime unit keeps a protected callback internal; anywhere else, and for a protected
    # definition without @callback, protected excludes extern "C".
    'protected-callback-outside-unit': (False, '''
@callback
@safety("PROBE", "None")
protected extern "C" void probe_work(raw void* context) {
}
i32 main() { return 0; }
''', [(4, 22, 'R-DIAG-FFI-003', 'R-FFI-0054',
       'C export requires a public definition, @safety, and a non-reserved C name')]),
    'protected-export-in-unit': (True, '''
@safety("R-RUNTIME-PROBE", "None")
protected extern "C" void r_runtime_probe_hidden() {
}
''', [(3, 22, 'R-DIAG-FFI-003', 'R-FFI-0054',
       'C export requires a public definition, @safety, and a non-reserved C name')]),
}

DIAGNOSTIC = re.compile(r'^(?P<path>.*?):(?P<line>\d+):(?P<column>\d+): error '
                        r'(?P<code>R-DIAG-[A-Z]+-\d+) \[(?P<rule>[A-Z0-9-]+)\]: (?P<message>.*)$')


def unit_arguments(args):
    return ['--runtime-unit', '--profile', 'freestanding', '--target-manifest',
            str(Path(args.targets) / 'arm64-apple-darwin.freestanding.json')]


def check_unit(args, root):
    path = root / 'unit.r'
    path.write_text('module runtime.probe_unit;\n' + UNIT.lstrip('\n'))
    result = subprocess.run(
        [args.front, '--emit=llvm-ir', '--opt-level=0', *unit_arguments(args), str(path)],
        capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr
    module = result.stdout
    # The objects keep their C names with external linkage; the function enters no preflight.
    assert re.search(r'^@r_runtime_probe_counter = (dso_local )?global ', module, re.MULTILINE)
    assert re.search(r'^@r_runtime_probe_limit = (dso_local )?global ', module, re.MULTILINE)
    assert 'r_static' not in module, module
    assert re.search(r'^define [^\n]*@r_runtime_probe_bump\(\)', module, re.MULTILINE), module
    assert 'atomicrmw add ptr @r_runtime_probe_counter' in module, module
    assert 'r_runtime_stack_require' not in module and 'r_runtime_c_entry' not in module, module
    # A protected callback is an internal C function named after its R function (R-FFI-0068);
    # the unit hands out its address, and no other object can name it.
    assert re.search(r'^define internal void @"runtime\.probe_unit::probe_work\.c"\(ptr ',
                     module, re.MULTILINE), module
    assert 'ret ptr @"runtime.probe_unit::probe_work.c"' in module, module
    assert not re.search(r'^define (dso_local )?void @probe_work\(', module, re.MULTILINE), module
    # Every entry of the unit is measured (B12.3, R-FFI-0068): its bound is reported, and one that
    # needs more than the call transition of the target is refused.
    assert re.search(r'^!r\.stack\.entries = ', module, re.MULTILINE), module
    assert '!{!"runtime.probe_unit::probe_work.c", i64 ' in module, module
    deep_path = root / 'unit_deep.r'
    deep_path.write_text('module runtime.probe_deep;\n' + DEEP_UNIT.lstrip('\n'))
    result = subprocess.run(
        [args.front, '--emit=llvm-ir', '--opt-level=0', *unit_arguments(args), str(deep_path)],
        capture_output=True, text=True, timeout=60)
    assert result.returncode != 0 and re.search(
        r'R-DIAG-LIMIT-001 \[R-FFI-0068\]: runtime entry r_runtime_probe_deep needs [0-9]+ bytes '
        r'of stack, above the call transition of 16384', result.stderr), result.stderr
    # A unit has no main (R-FUNC-0008, R-FFI-0068).
    main_path = root / 'unit_main.r'
    main_path.write_text('module runtime.probe_main;\ni32 main() { return 0; }\n')
    result = subprocess.run(
        [args.front, '--emit=llvm-ir', *unit_arguments(args), str(main_path)],
        capture_output=True, text=True, timeout=60)
    assert result.returncode == 1 and 'a runtime unit has no main' in result.stderr, result.stderr
    # The unit's options: the freestanding profile and no library.
    for extra in (['--runtime-unit'], ['--runtime-unit', '--profile', 'freestanding',
                                       '--library', 'probe']):
        result = subprocess.run([args.front, '--emit=llvm-ir', *extra, str(path)],
                                capture_output=True, text=True, timeout=60)
        assert result.returncode != 0 and ('--runtime-unit requires --profile freestanding and '
                                           'excludes --library') in result.stderr, result.stderr


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--targets', required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='r-runtime-unit-') as directory:
        root = Path(directory)
        check_unit(args, root)
        for name, (unit, text, expected) in REJECTED.items():
            path = root / f'{name}.r'
            path.write_text(f'module runtime.{name.replace("-", "_")};\n' + text.lstrip('\n'))
            command = [args.front, '--emit=hir', str(path)]
            if unit:
                command[2:2] = unit_arguments(args)
            result = subprocess.run(command, capture_output=True, text=True, timeout=60)
            assert result.returncode == 1, (name, result.returncode, result.stderr)
            found = sorted(
                (int(m['line']), int(m['column']), m['code'], m['rule'], m['message'])
                for m in map(DIAGNOSTIC.match, result.stderr.splitlines()) if m)
            assert found == sorted(expected), (name, found)
    print('runtime unit checks passed')


if __name__ == '__main__':
    main()
