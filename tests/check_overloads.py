#!/usr/bin/env python3
"""Exercise overload identity, selection, borrowing and definition-time checking."""
from pathlib import Path
import argparse
import subprocess
import tempfile

CASES = [
    ('arity', 'i32 pick(i32 x) { return x; } i32 pick(i32 x, i32 y) { return x+y; }',
     'i32 x = pick(1, 2); return x - 3;', None),
    ('exact', 'i32 pick(i32 x) { return x; } i32 pick(i64 x) { return 7; }',
     'i32 x = pick(2); return x - 2;', None),
    ('compatible', 'i32 pick(i64 x) { return 1; } i32 pick(str x) { return 2; }',
     'i32 x = pick(2); return x - 1;', None),
    ('ambiguous_conversion', 'i32 pick(i16 x) { return 1; } i32 pick(i32 x) { return 2; }',
     'i32 x = pick(2i8); return x;', 'ambiguous between overloads'),
    ('result_does_not_select', 'bool pick(i16 x) { return true; } i32 pick(i32 x) { return 2; }',
     'i32 x = pick(2i8); return x;', 'ambiguous between overloads'),
    ('duplicate_result', 'i32 pick(i32 x) { return x; } bool pick(i32 x) { return true; }',
     'return 0;', 'R-DIAG-NAME-002'),
    ('duplicate_alias', 'void pick(bytes x) {} void pick(array<u8> x) {}',
     'return 0;', 'R-DIAG-NAME-002'),
    ('duplicate_outer_const', 'void pick(i32 x) {} void pick(const i32 x) {}',
     'return 0;', 'R-DIAG-NAME-002'),
    ('duplicate_effects', 'error E {}; void pick(i32 x) {} void pick(i32 x) throws E {}',
     'return 0;', 'R-DIAG-NAME-002'),
    ('duplicate_async', 'void pick(i32 x) {} async void pick(i32 x) {}',
     'return 0;', 'R-DIAG-NAME-002'),
    ('duplicate_constraints', '@generic<T: copy> void pick(T x) {} @generic<U: pod> void pick(U x) {}',
     'return 0;', 'R-DIAG-NAME-002'),
    ('generic_ambiguity', '@generic<T: copy> i32 pick(T x) { return 1; } i32 pick(i32 x) { return 2; }',
     'i32 x = pick(1); return x;', 'ambiguous between overloads'),
    ('unused_definition', 'i32 pick(i32 x) { return x; } i32 pick(str x) { return 1; } '
     '@generic<T: copy> i32 unused(T x) { i32 result = pick(x); return result; }',
     'return 0;', 'no overload accepts'),
    ('conditional', 'i32 pick(i32 x) { return x; } i32 pick(bool x) { return 99; }',
     'bool flag = true; i32 x = pick((flag == true) ? 1 : 2); return x - 1;', 'R-DIAG-FLOW-001'),
    ('enum', 'enum Color { Red, Blue, }; i32 pick(Color x) { return 1; } i32 pick(i32 x) { return 2; }',
     'i32 x = pick(Color::Red); return x - 1;', None),
    ('payload_enum', 'enum Item { Number(i32), Empty, }; i32 pick(Item x) { return 1; } i32 pick(i32 x) { return 2; }',
     'i32 x = pick(Item::Number(4)); return x - 1;', None),
    ('aggregate', 'struct Point { i32 x; }; i32 pick(Point x) { return x.x; } i32 pick(i32 x) { return 9; }',
     'i32 x = pick(Point { .x = 3 }); return x - 3;', None),
    ('nested_standard', 'i32 pick(str x) { return 1; } i32 pick(i32 x) { return 2; }',
     'std.string::string text = std.string::create(); i32 x = pick(text.as_str()); return x - 1;', None),
    ('length', 'i32 pick(usize x) { return 1; } i32 pick(str x) { return 2; }',
     'str text = "test"; usize length = len(text); i32 x = pick(length); return x - 1;', None),
    ('fixed_array_view', 'i32 pick(const u8[] x) { return 1; } i32 pick(i32 x) { return 2; }',
     'u8[2] data = {1u8, 2u8}; i32 x = pick(data); return x - 1;', None),
    ('array_index', 'i32 pick(u8 x) { return 1; } i32 pick(i32 x) { return 2; }',
     'u8[2] data = {1u8, 2u8}; i32 x = pick(data[0usize]); return x - 1;', None),
    ('variadic', 'i32 pick(i32... values) { return 1; } i32 pick(str value) { return 2; }',
     'i32 x = pick(1, 2, 3); return x - 1;', None),
    ('variadic_empty', 'i32 pick(i32... values) { return 1; } i32 pick(str value) { return 2; }',
     'i32 x = pick(); return x - 1;', None),
    ('unhandled_selected_effect', 'error E {}; i32 pick(i32 x) { return x; } '
     'i32 pick(str x) throws E { throw E {}; }',
     'i32 x = pick("test"); return x;', 'R-DIAG-EFFECT-001'),
    ('rejected_effect', 'error E {}; i32 pick(i32 x) { return x; } '
     'i32 pick(str x) throws E { throw E {}; }',
     'i32 x = pick(0); return x;', None),
    ('mutable_receiver', 'struct Counter { i32 value; }; '
     'i32 Counter::read(Counter* this) { return 1; } '
     'i32 Counter::read(const Counter* this) { return 2; }',
     'Counter c = Counter { .value = 0 }; i32 x = c.read(); return x - 1;', None),
    ('shared_receiver', 'struct Counter { i32 value; }; '
     'i32 Counter::read(Counter* this) { return 1; } '
     'i32 Counter::read(const Counter* this) { return 2; }',
     'Counter c = Counter { .value = 0 }; const Counter* view = &c; '
     'i32 x = view->read(); return x - 2;', None),
    ('readonly_standard_receiver', '',
     'const std.format::builder b = std.format::create(); b.append("test"); return 0;', 'R-DIAG-BORROW'),
    ('method_no_implicit_move', '',
     'std.format::builder b = std.format::create(); std.string::string s = b.finish(); return 0;', 'R-DIAG-MOVE'),
    ('bad_append', '',
     'std.format::builder b = std.format::create(); b.append(true); return 0;', 'no append overload'),
    # M24-5 (R-FUNC-0004): the declared result of a user method or function selects.
    ('method_call_free_user', 'struct Point {}; i32 Point::value(const Point* this) { return 0; } '
     'i32 pick(i32 x) { return x; } i32 pick(str x) { return 1; }',
     'Point p = Point {}; i32 x = pick(p.value()); return x;', None),
    ('method_call_str_result', 'struct Point {}; str Point::name(const Point* this) { return "p"; } '
     'i32 pick(i32 x) { return x; } i32 pick(str x) { return 1; }',
     'Point p = Point {}; i32 x = pick(p.name()); return x - 1;', None),
    ('function_call_result', 'str label() { return "x"; } '
     'i32 pick(i32 x) { return x; } i32 pick(str x) { return 1; }',
     'i32 x = pick(label()); return x - 1;', None),
    ('small_promotion', 'i32 pick(i32 x) { return 1; } i32 pick(u8 x) { return 2; }',
     'u8 x = 2u8; i32 a = pick(x + x); i32 b = pick(-x); return a + b - 2;', None),
    ('c_promotion', 'i32 pick(c_int x) { return 1; } i32 pick(c_short x) { return 2; }',
     'c_short x = 2i32 as c_short; i32 a = pick(x + x); return a - 1;', None),
    ('nested_math', 'i32 pick(f32 x) { return 1; } i32 pick(f64 x) { return 2; }',
     'i32 a = pick(std.math::abs(-2.0f32)); return a - 1;', None),
    ('sizeof', 'i32 pick(usize x) { return 1; } i32 pick(i32 x) { return 2; }',
     'i32 a = pick(sizeof(i32)); return a - 1;', None),
    ('shared_field', 'struct P { i32 x; }; i32 pick(i32* x) { return 1; } i32 pick(const i32* x) { return 2; }',
     'P p = P { .x = 3 }; const P* shared = &p; i32 a = pick(&shared->x); return a - 2;', None),
    ('shared_index', 'i32 pick(u8* x) { return 1; } i32 pick(const u8* x) { return 2; }',
     'const u8[2] data = {1u8, 2u8}; i32 a = pick(&data[0usize]); return a - 2;', None),
    ('fixed_borrow_view', 'i32 pick(const u8[] x) { return 1; } i32 pick(i32 x) { return 2; }',
     'u8[2] data = {1u8, 2u8}; i32 a = pick(&data); return a - 1;', None),
    ('malformed_variadic', 'i32 pick(i32[]... x) { return 1; } i32 pick(str x) { return 2; }',
     'i32 a = pick(1); return a;', 'R-DIAG'),
    ('generic_closed_identity', '@generic<T: copy> i32 pick(T* x) { return 1; } '
     '@generic<U: copy> i32 pick(const U* x) { return 2; }',
     'i32 x = 1; i32 a = pick(&x); const i32* view = &x; i32 b = pick(view); return a + b - 3;', None),
    ('sync_initializer', 'i32 initialize(i32 x) { return x; } i32 initialize() { return 42; }',
     'std.sync::once_lock<i32> state = std.sync::once_lock::<i32>(); '
     'const i32* value = state.get_or_init(initialize); return *value - 42;', None),
    ('thread_entry', 'i32 worker(str x) { return 1; } i32 worker(i32 x) { return x; }',
     'try { std.thread::join_handle<i32> handle = std.thread::spawn(worker, 42); '
     '(move handle).detach(); } catch (std.thread::thread_error failure) {} return 0;', None),
    # R-NAME-0010 (M18): an implementation may repeat a registered standard method name; the
    # receiver form keeps the standard method and a constraint reaches the implementation.
    ('registry_trait_method', 'trait Sized { usize capacity(const Self* this); }; '
     'impl Sized for bytes { usize capacity(const bytes* this) { return 7usize; } }; '
     '@generic<T: Sized> usize traited(const T* value) { return value->capacity(); }',
     'bytes b = std.alloc::bytes(3usize, 0u8); usize inherent = b.capacity(); '
     'usize via = traited(&b); if (via * 100usize + inherent < 703usize) { return 1; } return 0;', None),

    ('nested_core', 'i32 pick(i32 x) { return x; } i32 pick(str x) { return 2; }',
     'i32 a = pick(core::wrapping_add(1, 2)); return a - 3;', None),
    ('nested_standard_comments', 'i32 pick(f32 x) { return 1; } i32 pick(f64 x) { return 2; }',
     'i32 a = pick(std.math /* operation */ ::abs(2.0f32)); return a - 1;', None),
    ('nested_hash', 'i32 pick(u32 x) { return 1; } i32 pick(str x) { return 2; }',
     'i32 a = pick(std.hash::crc32("test")); return a - 1;', None),

    ('new_owner', 'i32 pick(own i32* x) { return *x; } i32 pick(str x) { return 2; }',
     'i32 a = pick(new i32(42)); return a - 42;', None),
    ('formatted_argument', 'i32 pick(std.string::string x) { return 1; } i32 pick(i32 x) { return 2; }',
     'try { i32 a = pick(f"hello {1}".format(42)); return a - 1; } '
     'catch (std.alloc::alloc_error failure) { return 2; }', None),
    ('formatted_capture', 'i32 pick(std.string::string x) { return 1; } i32 pick(i32 x) { return 2; }',
     'i32 value = 42; try { i32 a = pick(f"hello {value}"); return a - 1; } '
     'catch (std.alloc::alloc_error failure) { return 2; }', None),
    ('nullable_argument', 'i32 pick(const i32*? x) { return 1; } i32 pick(i32 x) { return 2; }',
     'i32 a = pick(null); return a - 1;', None),
    ('nullable_ambiguity', 'i32 pick(const i32*? x) { return 1; } i32 pick(const i64*? x) { return 2; }',
     'i32 a = pick(null); return a;', 'ambiguous between overloads'),

    ('allocated_nominal_argument', 'struct P { i32 x; }; i32 pick(own P* x) { return x->x; } i32 pick(str x) { return 2; }',
     'try { i32 a = pick(std.alloc::try_new(P { .x = 42 })); return a - 42; } '
     'catch (std.alloc::new_error<P> failure) { return 2; }', None),


    ('null_exact', 'i32 pick(null_t x) { return 1; } i32 pick(const i32*? x) { return 2; } '
     'i32 pick(const i64*? x) { return 3; }',
     'i32 a = pick((null)); return a - 1;', None),
    ('null_distinct_from_pointer', 'i32 pick(null_t x) { return 1; } i32 pick(const i32*? x) { return 2; }',
     'const i32*? value = null; i32 a = pick(value); return a - 2;', None),
    ('null_generic_value_overload', '@generic<T: copy> i32 pick(T x) { return 2; } i32 pick(null_t x) { return 1; }',
     'i32 a = pick(null); return a - 1;', None),
    ('null_explicit_generic_parameter', '@generic<T: copy> i32 pick(null_t x, T value) { return 1; }',
     'i32 a = pick(null, 42); return a - 1;', None),
    ('null_generic_nullable_inference', '@generic<T: copy> i32 pick(const T*? x, T value) { return 1; } '
     'i32 pick(i32 first, i32 second) { return 2; }',
     'i32 a = pick(null, 42); return a - 1;', None),
    ('null_generic_uninferred', '@generic<T: copy> i32 pick(const T*? x) { return 1; } '
     'i32 pick(i32 value) { return 2; }', 'i32 a = pick(null); return a;', 'no overload accepts'),
    ('null_generic_nullable_single', '@generic<T: copy> i32 pick(const T*? x, T value) { return 1; }',
     'i32 a = pick(null, 42); return a - 1;', None),
    ('null_generic_computed_marker', '@generic<T: copy> void use(null_t x, T value) {}',
     'use(true ? null : null, 42); return 0;', 'R-DIAG-FLOW-001'),
    ('null_generic_wrong_conversion', '@generic<T: copy> void use(i32 x, T value) {}',
     'use(null, 42); return 0;', 'R-DIAG-TYPE-001'),
    ('null_generic_async_owner', '@generic<T: copy & send & unborrowed> '
     'async i32 use(own T*? x, T value) { return 1; }',
     'try { task<i32> result = use(null, 42); drop result; } '
     'catch (std.async::start_error failure) {} return 0;',
     None),
    ('null_const_parameter', 'i32 pick(const null_t x) { return 1; }',
     'i32 a = pick(null); return a - 1;', None),
    ('null_duplicate_const', 'void pick(null_t x) {} void pick(const null_t x) {}',
     'return 0;', 'R-DIAG-NAME-002'),
    ('null_pointer_argument', 'void use(null_t x) {}',
     'const i32*? p = null; use(p); return 0;', 'only the literal null'),
    ('null_integer_argument', 'void use(null_t x) {}',
     'use(0); return 0;', 'only the literal null'),
    ('null_boolean_argument', 'void use(null_t x) {}',
     'use(false); return 0;', 'only the literal null'),
    ('null_computed_argument', 'void use(null_t x) {}',
     'use(true ? null : null); return 0;', 'R-DIAG-FLOW-001'),
    ('null_parameter_read', 'void use(null_t x) { x as void; }',
     'return 0;', 'parameters have no value object'),
    ('null_parameter_address', 'void use(null_t x) { auto p = &x; }',
     'return 0;', 'parameters have no value object'),
    ('null_parameter_capture', 'void use(null_t x) { fn void nested() { x as void; } nested(); }',
     'return 0;', 'parameters have no value object'),
    ('null_parameter_capture_move', 'void use(null_t x) { fn void nested() move(x) { x as void; } nested(); }',
     'return 0;', 'parameters have no value object'),
    ('null_generic_unused_local', '@generic<T: copy> void use(T value) { auto absent = null; }',
     'return 0;', 'not an object type'),
    ('null_parameter_pointer', 'void use(null_t*? value) {}', 'return 0;', 'not an object type'),
    ('null_local', '', 'null_t x = null; return 0;', 'not an object type'),
    ('null_auto', '', 'auto x = null; return 0;', 'not an object type'),
    ('null_auto_computed', '', 'auto x = true ? null : null; return 0;', 'not an object type'),
    ('null_module_object', 'const null_t value = null;', 'return 0;', 'not an object type'),
    ('null_field', 'struct Slot { null_t value; };', 'return 0;', 'not an object type'),
    ('null_enum_payload', 'enum Item { Empty(null_t), };', 'return 0;', 'not an object type'),
    ('null_error_field', 'error Missing { null_t value; };', 'return 0;', 'not an object type'),
    ('null_function_result', 'null_t empty() { return null; }', 'return 0;', 'not an object type'),
    ('null_async_result', 'async null_t empty() { return null; }', 'return 0;', 'not an object type'),
    ('null_task_result', 'void use(task<null_t> value) {}', 'return 0;', 'not an object type'),
    ('null_generic_argument', '@generic<T> struct Box { T value; };',
     'Box<null_t> box = {}; return 0;', 'not an object type'),
    ('null_generic_inference', '@generic<T> void use(T value) {}',
     'use(null); return 0;', 'cannot be inferred'),
    ('null_variadic', 'void use(null_t... values) {}', 'return 0;', 'not an object type'),
    ('null_array', '', 'array<null_t> x = std.array::create::<null_t>(); return 0;', 'not an object type'),
    ('null_fixed_array', '', 'null_t[2] x = {null}; return 0;', 'not an object type'),
    ('null_slice', 'void use(const null_t[] values) {}', 'return 0;', 'not an object type'),
    ('null_pointer', 'void use(raw null_t* value) {}', 'return 0;', 'not an object type'),
    ('null_sizeof', '', 'usize size = sizeof(null_t); return 0;', 'not an object type'),
    ('null_option', '', 'o<null_t> value = o::none; return 0;', 'not an object type'),
    ('null_ffi_parameter', 'extern "C" void accept(null_t value) {}', 'return 0;', 'R-DIAG-FFI'),
    ('null_raw_function', 'void use(raw fn(null_t) -> void callback) {}', 'return 0;', 'not an object type'),
    ('null_keyword', '', 'i32 null_t = 1; return 0;', 'R-DIAG-SYN-001'),

]


def emit_options(mode):
    """Options of one output; LLVM IR lowers every function, not only those main reaches, so
    that code generation also accepts the functions main never calls."""
    return ['--emit=' + mode, '--all-functions'] if mode == 'llvm-ir' else ['--emit=' + mode]


def check_modules(frontend, directory):
    library = directory / 'library.r'
    main = directory / 'main.r'
    library.write_text('module check.library;\n'
                       'i32 pick(i32 x) { return x; }\n'
                       'i32 pick(str x) { return 42; }\n'
                       'i32 pick(null_t x) { return 0; }\n'
                       '@generic<T: copy> i32 inspect(T* x) { return 1; }\n'
                       '@generic<U: copy> i32 inspect(const U* x) { return 2; }\n')
    main.write_text('module check.main; import check.library;\n'
                    'i32 main() { i32 x = check.library::pick(42); '
                    'i32 a = check.library::inspect(&x); const i32* view = &x; '
                    'i32 b = check.library::inspect(view); i32 c = check.library::pick(null); '
                    'return x + a + b + c - 45; }\n')
    for emit in ('llvm-ir', 'interface'):
        outputs = []
        for sources in ((main, library), (library, main)):
            result = subprocess.run([str(frontend), *emit_options(emit), *map(str, sources)],
                                    capture_output=True, text=True, timeout=30)
            if result.returncode != 0:
                raise ValueError(f'module {emit}: {result.stderr}')
            outputs.append(result.stdout)
        if outputs[0] != outputs[1]:
            raise ValueError(f'{emit} changes when source modules are reordered')
        if emit == 'interface' and (outputs[0].count('overload="') < 4 or '(interface version=34' not in outputs[0]):
            raise ValueError('interface does not export the complete overload families')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--frontend', required=True, type=Path)
    args = parser.parse_args()
    failures = []
    with tempfile.TemporaryDirectory(prefix='r-overloads-') as directory:
        try:
            check_modules(args.frontend.resolve(), Path(directory))
        except ValueError as error:
            failures.append(str(error))
        for name, declarations, body, diagnostic in CASES:
            source = Path(directory) / (name + '.r')
            source.write_text('module check.overloads;\n' + declarations + '\ni32 main() { ' + body + ' }\n')
            result = subprocess.run([str(args.frontend.resolve()), *emit_options('llvm-ir'),
                                     str(source)],
                                    capture_output=True, text=True, timeout=30)
            if diagnostic is None:
                valid = result.returncode == 0
            else:
                valid = result.returncode == 1 and diagnostic in result.stderr
            if not valid:
                failures.append(f'{name}: expected {diagnostic or "accept"}, exit={result.returncode}\n{result.stderr}')
    if failures:
        raise SystemExit('\n'.join(failures))
    print(f'{len(CASES)} overload contract cases passed')


if __name__ == '__main__':
    main()
