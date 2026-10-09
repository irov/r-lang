#!/usr/bin/env python3
"""Check selected specified features through acceptance by the LLVM emitter (R to LLVM IR).

Every function of a probe is lowered (--all-functions), not only those main reaches.

Every probe is expected to compile. Unsupported features remain failures, rather
than passing negative tests. This bounded audit neither executes generated code
nor establishes full standard-library or runtime conformance.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile


PROBES = (
    (
        "multidimensional_array",
        "R-TYPE-0011",
        "struct Matrix { i32[2][2] values; };",
    ),
    (
        "signed_index",
        "R-EXPR-0021",
        "i32 read(const i32[] values, i32 index) { return values[index]; }",
    ),
    (
        "signed_range",
        "R-EXPR-0021",
        "const i32[] take(const i32[] values, i32 end) { return values[0..end]; }",
    ),
    (
        "mixed_compound",
        "R-EXPR-0013",
        "u64 add(u64 left, u32 right) { left += right; return left; }",
    ),
    (
        "small_compound_shift",
        "R-EXPR-0013",
        "u8 shift(u8 value) { value <<= 1usize; return value; }",
    ),
    (
        "char_switch",
        "R-STMT-0006",
        "i32 select(char value) { switch (value) {"
        " case 'a': return 1; default: return 0; } }",
    ),
    (
        "local_thread_storage",
        "R-OBJ-0008",
        "i32 count() { thread_local i32 value = 0; value += 1; return value; }",
    ),
    (
        "module_thread_storage",
        "R-OBJ-0008",
        "thread_local i32 counter = 0; i32 value() { return counter; }",
    ),
    (
        "mutable_module_object",
        "R-OBJ-0010",
        "i32 counter = 0;",
    ),
    (
        "local_atomic",
        "R-INIT-0012",
        "void create() { au32 value = 0; }",
    ),
    (
        "atomic_aggregate_local",
        "R-INIT-0012",
        "struct Counter { au32 value; }; void create() {"
        " Counter value = Counter { .value = 0 }; }",
    ),
    (
        "atomic_parameter",
        "R-TYPE-0013",
        "void consume(au32 value) {}",
    ),
    (
        "repr_c_struct",
        "R-FFI-0006",
        "@repr(C) struct Point { c_int x; c_int y; };",
    ),
    (
        "repr_c_enum",
        "R-AGG-0005",
        "@repr(C) enum Status : c_int { Ok = 0, Failed = 1 };",
    ),
    (
        "owned_array_range",
        "R-EXPR-0021",
        "const i32[] take(const array<i32>* values) {"
        " return (*values)[0usize..1usize]; }",
    ),
    (
        "conditional_borrow_origins",
        "R-BORROW-0008",
        "const i32* choose(bool pick, const i32* first, const i32* second) {"
        " const i32* chosen = (pick == true) ? first : second; return chosen; }",
    ),
    (
        "return_borrow_origins",
        "R-BORROW-0008",
        "const i32* choose(bool pick, const i32* first, const i32* second) {"
        " if (pick == true) { return first; } return second; }",
    ),
    (
        "enum_fallthrough",
        "R-STMT-0007",
        "enum Mode { A, B, C }; i32 select(Mode mode) { switch (mode) {"
        " case variant Mode::A: fallthrough; case variant Mode::B: return 1;"
        " case variant Mode::C: return 0; } }",
    ),
    (
        "network_address",
        "R-SLIB-NET-0006",
        "std.net::socket_address endpoint(const std.net::tcp_stream* stream)"
        " throws std.net::net_error { std.net::socket_address value ="
        " std.net::tcp_local_address(stream); return value; }",
    ),
    (
        "process_builder",
        "R-SLIB-PROC-0002",
        "void configure(std.process::command* command) {"
        " std.process::clear_environment(command); }",
    ),
    (
        "filesystem_metadata",
        "R-SLIB-FS-0006",
        "async void inspect(std.fs::file file) throws std.fs::fs_error,"
        " std.async::start_error { std.fs::metadata value ="
        " await std.fs::file_metadata(&file, o::none); value as void; }",
    ),
    (
        "control_usize_index",
        "R-EXPR-0021",
        "i32 read(const i32[] values, usize index) { return values[index]; }",
    ),
    (
        "control_same_width_compound",
        "R-EXPR-0013",
        "u64 add(u64 left, u64 right) { left += right; return left; }",
    ),
    (
        "control_mixed_comparison",
        "R-EXPR-0003",
        "bool compare(u32 left, u64 right) { return left < right; }",
    ),
    (
        "control_mixed_bitwise",
        "R-EXPR-0003",
        "u64 combine(u32 left, u64 right) { return left | right; }",
    ),
    (
        "module_negative_initializer",
        "R-OBJ-0010",
        "i32 value = -1;",
    ),
    (
        "module_bool_initializer",
        "R-OBJ-0010",
        "bool value = true;",
    ),
    (
        "module_aggregate_initializer",
        "R-OBJ-0010",
        "struct Pair { i32 left; i32 right; }; "
        "Pair value = Pair { .left = 1, .right = 2 };",
    ),
    (
        "module_thread_aggregate",
        "R-OBJ-0008",
        "struct Counter { i32 value; }; "
        "thread_local Counter counter = Counter { .value = 1 };",
    ),
    (
        "block_static_aggregate",
        "R-OBJ-0010",
        "struct Counter { i32 value; }; "
        "void create() { static Counter counter = { .value = 1 }; }",
    ),
    (
        "core_atomic_load",
        "R-LIB-0011",
        "u32 read() { au32 value = 7; return core::atomic_load("
        "&value, core::memory_order::relaxed); }",
    ),
    (
        "enum_reflection",
        "R-REFL-0001",
        "enum Color { red, green, blue, }; "
        "usize probe(Color color) { Color[3] all = core::enum_variants::<Color>(); "
        "constexpr str name = core::enum_name(color); o<Color> at = core::enum_at::<Color>(1); "
        "o<Color> found = core::enum_from_name::<Color>(\"blue\"); name as void; at as void; "
        "found as void; if (all[0] != core::enum_min::<Color>()) { return 0; } "
        "return core::enum_count::<Color>() + core::enum_ordinal(color); }",
    ),
    (
        "variant_reflection",
        "R-REFL-0002",
        "enum Shape { dot, circle(f32), }; "
        "usize probe(const Shape* shape) { constexpr str name = core::variant_name(shape); "
        "name as void; return core::variant_count::<Shape>(); }",
    ),
    (
        "type_reflection",
        "R-REFL-0003",
        "struct Point { i32 x; i32 y; }; "
        "@generic<T: copy> constexpr str describe(T value) { "
        "constexpr str name = core::type_name::<T>(); value as void; return name; } "
        "usize probe() { constexpr str spelled = describe(7i32); "
        "constexpr str field = core::field_name::<Point>(1); constexpr str target = core::target_name(); "
        "spelled as void; field as void; target as void; return core::field_count::<Point>(); }",
    ),
    (
        "core_checked_add",
        "R-LIB-0001",
        "o<i32> add(i32 left, i32 right) { "
        "return core::checked_add_i32(left, right); }",
    ),
    (
        "core_wrapping_add",
        "R-LIB-0001",
        "i32 add(i32 left, i32 right) { "
        "return core::wrapping_add_i32(left, right); }",
    ),
    (
        "core_saturating_add",
        "R-LIB-0001",
        "i32 add(i32 left, i32 right) { "
        "return core::saturating_add_i32(left, right); }",
    ),
    (
        "compound_borrow_output",
        "R-BORROW-0008",
        "struct View { const i32* value; }; "
        "View borrow(const i32* value) { return View { .value = value }; }",
    ),
    (
        "multi_origin_checked_error_borrow",
        "R-BORROW-0008",
        "error BorrowError { const i32* value; }; "
        "void fail(bool pick, const i32* first, const i32* second) throws BorrowError { "
        "throw BorrowError { .value = pick == true ? first : second }; }",
    ),
    (
        "module_move_object",
        "R-OBJ-0010",
        "bytes value = {};",
    ),
    (
        "module_const_move_object",
        "R-OBJ-0010",
        "const bytes value = {};",
    ),
    (
        "module_thread_move_object",
        "R-OBJ-0008",
        "thread_local bytes value = {};",
    ),
    (
        "block_static_move_object",
        "R-OBJ-0010",
        "void create() { static bytes value = {}; }",
    ),
    (
        "block_thread_move_object",
        "R-OBJ-0008",
        "void create() { thread_local bytes value = {}; }",
    ),
    (
        "open_range_upper_bound",
        "R-EXPR-0021",
        "const i32[] tail(const i32[] values) { return values[1..]; }",
    ),
    (
        "open_range_lower_bound",
        "R-EXPR-0021",
        "const i32[] head(const i32[] values) { return values[..1]; }",
    ),
    (
        "module_constant_expression",
        "R-INIT-0002",
        "const i32 base = 4; const i32 limit = (base + 1) * 2 - 1; "
        "i32 value() { return limit; }",
    ),
    (
        "small_integer_negation",
        "R-EXPR-0003",
        "i32 negate(u8 value) { return -value; }",
    ),
    (
        "fixed_array_owner_move",
        "R-INIT-0007",
        "struct Node { i64 value; (own Node*?)[2] children; }; "
        "void nest(own Node*? leaf) { (own Node*?)[2] children = {}; "
        "children[0] = move leaf; "
        "own Node*? parent = new Node { .value = 2, .children = move children }; "
        "drop parent; }",
    ),
    (
        "explicit_panic",
        "R-ERR-0004",
        "i32 fail(str message) { panic(message); }",
    ),
    (
        "never_statement_path",
        "R-FUNC-0003",
        "i32 stop(bool halt) { if (halt == true) { panic(\"stop\"); } return 0; }",
    ),
    (
        "char_integer_cast",
        "R-EXPR-0016",
        "u32 code(char value) { return value as u32; } "
        "char scalar(u32 value) { return value as char; }",
    ),
    (
        "repr_c_enum_cast",
        "R-EXPR-0018",
        "@repr(C) enum Kind : c_int { Zero, One, }; "
        "i32 number(Kind kind) { return kind as i32; } "
        "Kind decode(i32 value) { return value as Kind; }",
    ),
    (
        "char_comparison",
        "R-EXPR-0009",
        "bool before(char left, char right) { return left < right; }",
    ),
    (
        "struct_method",
        "R-FUNC-0013",
        "struct Point { i32 x; i32 y; }; "
        "i32 Point::sum(const Point* this) { return this->x + this->y; } "
        "void Point::shift(Point* this, i32 dx) { this->x += dx; }",
    ),
    (
        "method_call",
        "R-FUNC-0014",
        "struct Point { i32 x; }; "
        "i32 Point::get(const Point* this) { return this->x; } "
        "i32 read(Point value) { i32 result = value.get(); return result; }",
    ),
    (
        "associated_function",
        "R-FUNC-0013",
        "struct Point { i32 x; }; "
        "Point Point::origin() { return Point { .x = 0 }; } "
        "i32 start() { Point value = Point::origin(); return value.x; }",
    ),
    (
        "trait_impl",
        "R-TYPE-0042",
        "struct Point { i32 x; }; "
        "trait Shown { u64 show(const Self* this); }; "
        "impl Shown for Point { u64 show(const Point* this) { return 1u64; } };",
    ),
    (
        "trait_bound",
        "R-TYPE-0043",
        "struct Point { i32 x; }; "
        "trait Shown { u64 show(const Self* this); }; "
        "impl Shown for Point { u64 show(const Point* this) { return 1u64; } }; "
        "@generic<T: Shown> u64 show_of(const T* value) { u64 code = value->show(); return code; }",
    ),
    (
        "scalar_impl",
        "R-TYPE-0042",
        "trait Shown { u64 show(const Self* this); }; "
        "impl Shown for i32 { u64 show(const i32* this) { return *this as u64; } };",
    ),
    (
        "local_lambda",
        "R-FUNC-0015",
        "i32 compute() { i32 offset = 1; fn i32 add(i32 x) { return x + offset; } "
        "i32 result = add(2); return result; }",
    ),
    (
        "moved_capture",
        "R-FUNC-0016",
        "i32 compute() { own i32* boxed = new i32(3); "
        "fn i32 unbox() move(boxed) { i32 value = *boxed; return value; } "
        "i32 result = unbox(); return result; }",
    ),
    (
        "callable_constraint",
        "R-TYPE-0044",
        "@generic<F: fn(i32) -> i32> i32 apply(const F* f, i32 x) { i32 r = f(x); return r; } "
        "i32 compute() { fn i32 twice(i32 x) { return x + x; } i32 r = apply(&twice, 2); "
        "return r; }",
    ),
    (
        "auto_local",
        "R-NAME-0011",
        "i32 compute() { auto value = 4; return value; }",
    ),
    (
        "range_for",
        "R-STMT-0014",
        "i32 compute() { i32 total = 0; i32[2] pair = {1, 2}; "
        "for (i32 i in 0..3) { total += i; } "
        "for (const i32* x in &pair) { total += *x; } return total; }",
    ),
    (
        "core_iterator",
        "R-TYPE-0046",
        "struct Counter { i32 next_value; i32 limit; }; "
        "impl core::Iterator for Counter { type Item = i32; o<i32> next(Counter* this) { "
        "if (this->next_value >= this->limit) { return o::none; } i32 value = this->next_value; "
        "this->next_value += 1; return o::some(value); } }; "
        "i32 compute() { Counter c = Counter { .next_value = 0, .limit = 3 }; i32 total = 0; "
        "for (i32 v in move c) { total += v; } return total; }",
    ),
    (
        "associated_type",
        "R-TYPE-0045",
        "trait Source { type Item; o<Self::Item> pull(Self* this); }; "
        "struct Counter { i32 next_value; }; "
        "impl Source for Counter { type Item = i32; o<i32> pull(Counter* this) { "
        "return o::none; } }; "
        "@generic<S: Source> o<S::Item> first(S* source) { o<S::Item> v = source->pull(); "
        "return move v; }",
    ),
    (
        "membership",
        "R-EXPR-0029",
        "struct Digits { i32[3] values; }; "
        "impl core::Contains for Digits { type Item = i32; "
        "bool contains(const Digits* this, const i32* value) { "
        "for (const i32* digit in &this->values) { if (*digit == *value) { return true; } } "
        "return false; } }; "
        "i32 compute() { Digits digits = Digits { .values = {1, 2, 3} }; i32 two = 2; "
        "if (two not in digits) { return 1; } if (two in 0..2) { return 2; } return 0; }",
    ),
    (
        "collection_literal",
        "R-EXPR-0030",
        "i32 compute() { i32 total = 0; try { array<i32> small = [1, 2, 3]; "
        "for (const i32* x in &small) { total += *x; } } "
        "catch (std.array::push_error<i32> failure) { failure as void; return 1; } "
        "return total; }",
    ),
    (
        "comprehension",
        "R-EXPR-0030",
        "i32 compute() { i32 total = 0; try { "
        "array<i32> squares = [x * x for (i32 x in 0..10) if (x % 2 == 0)]; "
        "for (const i32* x in &squares) { total += *x; } } "
        "catch (std.array::push_error<i32> failure) { failure as void; return 1; } "
        "return total; }",
    ),
    (
        "dict_expression",
        "R-EXPR-0030",
        "i32 compute() { try { dict<i32, i32> doubles = {x: x * 2 for (i32 x in 0..4)}; "
        "dict<i32, i32> fixed = {1: 10, 2: 20}; i32 two = 2; "
        "if (two not in doubles) { drop fixed; return 1; } if (two not in fixed) { return 2; } } "
        "catch (std.dict::insert_error<i32, i32> failure) { failure as void; return 3; } "
        "return 0; }",
    ),
    (
        "variadic_parameter",
        "R-FUNC-0018",
        "i32 sum(i32... values) { i32 total = 0; "
        "for (const i32* v in &values) { total += *v; } return total; } "
        "i32 compute() { i32[2] pair = {4, 5}; const i32[] view = &pair; "
        "i32 a = sum(1, 2, 3); i32 b = sum(); i32 c = sum(...view); return a + b + c; }",
    ),
    (
        "borrow_fields",
        "R-BORROW-0018",
        "struct Holder { const i32* value; }; "
        "i32 Holder::get(const Holder* this) { i32 v = *(this->value); return v; } "
        "Holder wrap(const i32* value) { return Holder { .value = value }; } "
        "i32 compute() { i32 x = 4; Holder h = wrap(&x); i32 a = h.get(); return a; }",
    ),
    (
        "iterator_adapter",
        "R-TYPE-0044",
        "struct Counter { i32 next_value; i32 limit; }; "
        "impl core::Iterator for Counter { type Item = i32; o<i32> next(Counter* this) { "
        "if (this->next_value >= this->limit) { return o::none; } i32 v = this->next_value; "
        "this->next_value += 1; return o::some(v); } }; "
        "@generic<I: core::Iterator, U, F: fn(I::Item) -> U> "
        "struct map_iter { I inner; const F* function; }; "
        "@generic<I: core::Iterator, U, F: fn(I::Item) -> U> "
        "impl core::Iterator for map_iter<I, U, F> { type Item = U; "
        "o<U> next(map_iter<I, U, F>* this) { o<I::Item> item = this->inner.next(); "
        "switch (move item) { case variant o::some(move v): U mapped = this->function(move v); "
        "return o::some(move mapped); case variant o::none: return o::none; } } }; "
        "@generic<I: core::Iterator, U, F: fn(I::Item) -> U> "
        "map_iter<I, U, F> map(I inner, const F* function) { "
        "return map_iter<I, U, F> { .inner = move inner, .function = function }; } "
        "i32 compute() { Counter c = Counter { .next_value = 0, .limit = 4 }; "
        "fn i32 twice(i32 x) { return x * 2; } auto it = map(move c, &twice); i32 total = 0; "
        "for (i32 v in &it) { total += v; } return total; }",
    ),
    (
        "generic_owner_spelling",
        "R-FUNC-0013",
        "@generic<T: key> struct set { dict<T, bool> entries; }; "
        "@generic<T: key> set<T> set<T>::create() { dict<T, bool> entries = "
        "std.dict::create::<T, bool>(); return set<T> { .entries = move entries }; } "
        "@generic<T: key> usize set<T>::count(const set<T>* this) { usize n = len(this->entries); "
        "return n; } "
        "i32 compute() { set<i32> s = set<i32>::create(); usize n = s.count(); return n as i32; }",
    ),
    (
        "associated_constant",
        "R-TYPE-0050",
        "trait Encoded { const usize SIZE; const bool PACKED = false; }; "
        "struct Point { u32 x; u32 y; }; "
        "impl Encoded for Point { const usize SIZE = sizeof(Self); }; "
        "@generic<T: Encoded> struct Frame { u8[T::SIZE] payload; }; "
        "@generic<T: Encoded> usize bytes() { @if (T::PACKED == true) { return T::SIZE; } "
        "@else { return T::SIZE * 2usize; } } "
        "i32 compute() { Frame<Point> frame = {}; usize n = len(frame.payload) + bytes::<Point>(); "
        "return n as i32; }",
    ),
    (
        "dyn_interface",
        "R-TYPE-0051",
        "trait Store { u32 get(const Self* this, u32 key); void put(Self* this, u32 value); }; "
        "struct Memory { u32 value; }; struct Doubled { u32 value; }; "
        "impl Store for Memory { u32 get(const Memory* this, u32 key) { return this->value + key; } "
        "void put(Memory* this, u32 value) { this->value = value; } }; "
        "impl Store for Doubled { u32 get(const Doubled* this, u32 key) { return this->value * 2u32; } "
        "void put(Doubled* this, u32 value) { this->value = value; } }; "
        "u32 use(dyn(Store)* store) { store->put(3u32); const dyn(Store)* view = store; "
        "return view->get(1u32); } "
        "i32 compute() { Memory memory = {.value = 0u32}; Doubled doubled = {.value = 0u32}; "
        "u32 total = use(&memory) + use(&doubled); return total as i32; }",
    ),
    (
        "task_select",
        "R-STMT-0018",
        "@scoped async u32 read(const u32* value) throws std.async::start_error { return *value; } "
        "async u32 race(std.time::instant deadline) throws std.async::start_error { "
        "u32 left = 1u32; u32 right = 2u32; u32 total = 0u32; task_scope(2) group { "
        "auto a = read(&left); auto b = read(&right); "
        "o<usize> ready = await group.first_until(deadline, &a, &b); ready as void; "
        "select (group) { case u32 value = await move a: total += value; break; "
        "case u32 value = await move b: total += value; break; "
        "case until (deadline): break; } "
        "group.cancel_all(); await group.all(); } return total; }",
    ),
)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=root / "build-debug/r-front")
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    if not compiler.is_file():
        parser.error(f"compiler does not exist: {compiler}")

    results = []
    compiler_hash = hashlib.sha256(compiler.read_bytes()).hexdigest()
    with tempfile.TemporaryDirectory(prefix="r-implementation-audit-") as temporary:
        directory = Path(temporary)
        for name, rule, body in PROBES:
            source = (
                f"module implementation_audit.{name};\n"
                f"{body}\n"
                "i32 main() { return 0; }\n"
            )
            path = directory / f"{name}.r"
            path.write_text(source, encoding="utf-8")
            record = {"name": name, "rule": rule, "source": source}
            try:
                process = subprocess.run(
                    [
                        str(compiler),
                        "--profile=hosted-native-async",
                        "--emit=llvm-ir",
                        "--all-functions",
                        "--diagnostics=json",
                        str(path),
                    ],
                    capture_output=True,
                    text=True,
                    timeout=30,
                    check=False,
                    cwd=root,
                )
            except subprocess.TimeoutExpired:
                record.update(status="timeout", diagnostics=[])
                results.append(record)
                continue
            record["returncode"] = process.returncode
            record["status"] = (
                "accepted"
                if process.returncode == 0 and process.stdout.strip()
                else "rejected" if process.returncode == 1 else "compiler_failure"
            )
            try:
                diagnostics = json.loads(process.stderr)
            except json.JSONDecodeError:
                diagnostics = [{"message": process.stderr.strip()}]
            for diagnostic in diagnostics:
                if "source" in diagnostic:
                    diagnostic["source"] = path.name
            record["diagnostics"] = diagnostics
            results.append(record)

    failed = sum(record["status"] != "accepted" for record in results)
    print(
        json.dumps(
            {
                "scope": "selected specified features, R-to-LLVM-IR acceptance only",
                "compiler": str(compiler),
                "compiler_sha256": compiler_hash,
                "compiled_objects": False,
                "executed_generated_programs": False,
                "probes": len(results),
                "accepted": len(results) - failed,
                "failed": failed,
                "results": results,
            },
            indent=2,
        )
    )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
