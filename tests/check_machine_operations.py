#!/usr/bin/env python3
"""Check the machine operations, address constants and signal handlers (Library R-LIB-0028, Core
R-UNSAFE-0008, R-OBJ-0013, R-FFI-0067, B11): each rejection at its exact position with its code,
rule and message; the LLVM forms of the operations in the optimized IR of their fixture; and a
signal handler whose functions call no runtime entry but the aborting panic (no thread attachment,
stack bound, C-call bracket or unwind test)."""

import argparse
from pathlib import Path
import re
import subprocess
import tempfile

FENCE_ORDER = 'a fence requires acquire, release, acq_rel or seq_cst'
FENCE_ARITY = 'a core fence takes one core::memory_order'
ATTRIBUTE = 'invalid, duplicate, or misplaced function attribute'
C_ATTRIBUTE = 'invalid, duplicate, or misplaced C import attribute'
UNPROVED = '@signal_safe cannot be proved: '
STATIC_ACCESS = ('direct mutable static access requires unsafe; '
                 'immutable static access requires Sync')
RAW_CONVERSION = 'raw pointer conversion is outside an unsafe context'

# name -> (source, [(line, column, code, rule, message)]); line 1 is the module declaration.
REJECTED = {
    'operations': ('''
u64 cell = 0u64;
protected void fences(core::memory_order order) {
    core::atomic_fence(core::memory_order::relaxed);
    core::signal_fence(core::memory_order::relaxed);
    core::atomic_fence(1u32);
    core::atomic_fence();
    core::signal_fence(order, order);
}
protected u32 unchecked(u32 x) {
    if (x > 1u32) {
        return x;
    }
    core::unreachable();
}
protected void prefetches(u64 value) {
    unsafe {
        core::prefetch_write(&cell as raw const u64*);
        core::prefetch_read(value);
        core::prefetch_read();
    }
}
protected usize reads() {
    return core::stack_pointer(1usize) + core::frame_address();
}
@signal_handler
protected void handler(c_int number) { number as void; }
@signal_handler
@callback
@safety("BOTH", "none")
extern "C" void both(c_int number) { number as void; }
i32 main() { return 0; }
''', [(4, 24, 'R-DIAG-ATOMIC-001', 'R-MEM-0007', FENCE_ORDER),
      (5, 24, 'R-DIAG-ATOMIC-001', 'R-MEM-0007', FENCE_ORDER),
      (6, 5, 'R-DIAG-TYPE-001', 'R-LIB-0028',
       'a core fence order shall have type core::memory_order'),
      (7, 5, 'R-DIAG-TYPE-001', 'R-LIB-0028', FENCE_ARITY),
      (8, 5, 'R-DIAG-TYPE-001', 'R-LIB-0028', FENCE_ARITY),
      (14, 11, 'R-DIAG-UNSAFE-001', 'R-UNSAFE-0001',
       'core::unreachable is outside an unsafe context'),
      (18, 9, 'R-DIAG-TYPE-001', 'R-LIB-0028', 'core::prefetch_write requires raw T*'),
      (19, 9, 'R-DIAG-TYPE-001', 'R-LIB-0028', 'core::prefetch_read requires a raw pointer'),
      (20, 9, 'R-DIAG-TYPE-001', 'R-LIB-0028', 'a core prefetch takes one raw pointer'),
      (24, 12, 'R-DIAG-TYPE-001', 'R-LIB-0028', 'this core machine operation takes no argument'),
      (26, 1, 'R-DIAG-SYN-002', 'R-GRAM-0007', ATTRIBUTE),
      (29, 1, 'R-DIAG-FFI-003', 'R-FFI-0050', ATTRIBUTE)]),
    'signals': ('''
import std.string;
import std.time;
@header("stdlib.h")
extern "C" {
    @safety("ABORT", "none")
    never abort();
    @signal_safe
    @safety("EXIT", "none")
    never _Exit(c_int status);
}
thread_local u32 per_thread = 0u32;
u32 shared = 0u32;
protected u32 plain(u32 x) { return x + 1u32; }
@signal_safe
protected u32 calls_plain(u32 x) { return plain(x); }
@signal_safe
protected void thread_state() { per_thread += 1u32; }
@signal_safe
protected c_int through(raw fn(c_int) -> c_int f) { unsafe { return f(1i32 as c_int); } }
@signal_safe
protected async u32 later() { return 1u32; }
@signal_safe
@recursion(depth = 4)
protected u32 down(u32 n) throws core::recursion_error {
    if (n == 0u32) {
        return 0u32;
    }
    return down(n - 1u32);
}
@signal_safe
protected std.time::duration span() { return std.time::duration_from_seconds(5i64); }
@signal_safe
protected void take(std.string::string text) { drop text; }
@signal_safe
protected void leave() { unsafe { abort(); } }
@signal_safe
protected void quit() { unsafe { _Exit(1i32 as c_int); } }
@signal_safe
protected u32 mix(u32 x, u32 y) { unsafe { shared += 1u32; } return core::rotate_left(x, 3u32); }
@signal_handler
@safety("ON-SIGNAL", "none")
extern "C" void on_signal(c_int number) { plain(number as u32) as void; }
i32 main() { return 0; }
''', [(16, 43, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + 'calls_plain -> the callee is not @signal_safe'),
      (18, 33, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + 'thread_state -> a thread_local object is not signal-safe'),
      (20, 69, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + 'through -> a call through a pointer is not signal-safe'),
      (22, 21, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + 'an async function is not signal-safe'),
      (25, 15, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + '@recursion counts its depth in run-time state'),
      (32, 46, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + 'span -> this standard operation is not signal-safe'),
      (34, 21, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + 'take -> a value with cleanup code is not signal-safe'),
      (36, 35, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + 'leave -> the callee is not @signal_safe'),
      (43, 43, 'R-DIAG-RESOURCE-003', 'R-FFI-0067',
       UNPROVED + 'on_signal -> the callee is not @signal_safe')]),
    'attributes': ('''
@header("stdlib.h")
extern "C" {
    @signal_safe(fast)
    @safety("ABORT", "none")
    never abort();
    @signal_handler
    @safety("EXIT", "none")
    never _Exit(c_int status);
}
@signal_handler(x)
@safety("A", "none")
extern "C" void handler_arguments(c_int number) { number as void; }
@signal_safe(x)
protected void safe_arguments() { }
@signal_safe
@signal_safe
protected void twice() { }
@signal_safe
@safety("B", "none")
extern "C" void plain_export(c_int number) { number as void; }
i32 main() { return 0; }
''', [(4, 5, 'R-DIAG-FFI-003', 'R-FFI-0050', C_ATTRIBUTE),
      (7, 5, 'R-DIAG-FFI-003', 'R-FFI-0050', C_ATTRIBUTE),
      (11, 1, 'R-DIAG-FFI-003', 'R-FFI-0050', ATTRIBUTE),
      (14, 1, 'R-DIAG-SYN-002', 'R-GRAM-0007', ATTRIBUTE),
      (17, 1, 'R-DIAG-SYN-002', 'R-GRAM-0007', ATTRIBUTE)]),
    'addresses': ('''
u32[4] cells = {};
usize position = 2usize;
thread_local u32 per_thread = 0u32;
raw u32* computed = &cells[position] as raw u32*;
raw u32* local_thread = &per_thread as raw u32*;
u32 copied = cells[1];
protected u32 inside() {
    thread_local u32 mine = 0u32;
    static raw u32* where = &mine as raw u32*;
    unsafe {
        return *where;
    }
}
i32 main() { return 0; }
''', [(5, 38, 'R-DIAG-UNSAFE-001', 'R-UNSAFE-0001', RAW_CONVERSION),
      (5, 22, 'R-DIAG-MEM-001', 'R-OBJ-0009', STATIC_ACCESS),
      (5, 28, 'R-DIAG-MEM-001', 'R-OBJ-0009', STATIC_ACCESS),
      (6, 37, 'R-DIAG-UNSAFE-001', 'R-UNSAFE-0001', RAW_CONVERSION),
      (7, 14, 'R-DIAG-MEM-001', 'R-OBJ-0009', STATIC_ACCESS),
      (10, 35, 'R-DIAG-UNSAFE-001', 'R-UNSAFE-0001', RAW_CONVERSION),
      (10, 5, 'R-DIAG-INIT-001', 'R-NAME-0008', 'static object requires a constant initializer')]),
}

# The optimized IR of codegen_machine_operations.r contains each of these forms.
MACHINE_FORMS = ('fence acquire', 'fence release', 'fence acq_rel', 'fence seq_cst',
                 'fence syncscope("singlethread") acquire', 'fence syncscope("singlethread") release',
                 'fence syncscope("singlethread") acq_rel',
                 'fence syncscope("singlethread") seq_cst', '@llvm.frameaddress.p0(',
                 '@llvm.read_register.i64(', '@llvm.prefetch.p0(')

# The functions of codegen_signal_handler.r that run in the handler.
SIGNAL_FUNCTIONS = ('@"test.codegen.signal_handler::note"(',
                    '@"test.codegen.signal_handler::slot_of"(',
                    '@"test.codegen.signal_handler::on_signal"(', '@on_signal(')
RUNTIME_REFERENCE = re.compile(r'@r_runtime_[a-z_]+')

DIAGNOSTIC = re.compile(r'^(?P<path>.*?):(?P<line>\d+):(?P<column>\d+): error '
                        r'(?P<code>R-DIAG-[A-Z]+-\d+) \[(?P<rule>[A-Z0-9-]+)\]: (?P<message>.*)$')


def functions(module):
    """The text of each function defined in an LLVM module, by its define line."""
    found = {}
    for match in re.finditer(r'^define [^\n]*\{\n.*?^\}\n', module, re.MULTILINE | re.DOTALL):
        text = match.group(0)
        found[text.splitlines()[0]] = text
    return found


def check_ir(args):
    machine = subprocess.run(
        [args.front, '--emit=llvm-ir', '--library-map', args.library_map,
         str(Path(args.fixtures) / 'codegen_machine_operations.r')],
        capture_output=True, text=True, timeout=120)
    assert machine.returncode == 0, machine.stderr
    for form in MACHINE_FORMS:
        assert form in machine.stdout, form
    trap = subprocess.run(
        [args.front, '--emit=llvm-ir', '--library-map', args.library_map,
         str(Path(args.fixtures) / 'codegen_machine_trap.r')],
        capture_output=True, text=True, timeout=120)
    assert trap.returncode == 0 and '@llvm.trap(' in trap.stdout, trap.stderr
    sysroot = subprocess.run(['xcrun', '--sdk', 'macosx', '--show-sdk-path'], capture_output=True,
                             text=True, check=True).stdout.strip()
    signal = subprocess.run(
        [args.front, '--emit=llvm-ir', '--opt-level=0', '--library-map', args.library_map,
         '--link-manifest', str(Path(args.fixtures) / 'codegen_signal_handler_link_manifest.json'),
         '--sysroot', sysroot, str(Path(args.fixtures) / 'codegen_signal_handler.r')],
        capture_output=True, text=True, timeout=120)
    assert signal.returncode == 0, signal.stderr
    defined = functions(signal.stdout)
    for name in SIGNAL_FUNCTIONS:
        bodies = [text for line, text in defined.items() if name in line]
        assert len(bodies) == 1, (name, list(defined))
        references = set(RUNTIME_REFERENCE.findall(bodies[0]))
        assert references <= {'@r_runtime_panic'}, (name, sorted(references))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--front', required=True)
    parser.add_argument('--library-map', required=True)
    parser.add_argument('--fixtures', required=True)
    args = parser.parse_args()
    check_ir(args)
    with tempfile.TemporaryDirectory(prefix='r-machine-') as directory:
        root = Path(directory)
        for name, (text, expected) in REJECTED.items():
            path = root / f'{name}.r'
            path.write_text(f'module machine.{name};\n' + text.lstrip('\n'))
            result = subprocess.run(
                [args.front, '--emit=mir', '--library-map', args.library_map, str(path)],
                capture_output=True, text=True, timeout=60)
            assert result.returncode == 1, (name, result.returncode, result.stderr)
            found = sorted(
                (int(m['line']), int(m['column']), m['code'], m['rule'], m['message'])
                for m in map(DIAGNOSTIC.match, result.stderr.splitlines()) if m)
            assert found == sorted(expected), (name, found)
    print('machine operation checks passed')


if __name__ == '__main__':
    main()
