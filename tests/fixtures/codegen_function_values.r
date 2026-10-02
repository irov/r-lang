module test.codegen.function_values;

// R-TYPE-0054 (L28): function values in tables, fields, containers, constants, generic code and
// resource-checked code; each check reports its own exit code.

error Rejected { i32 code; };

i32 add(i32 a, i32 b) { return a + b; }
i32 sub(i32 a, i32 b) { return a - b; }
i32 mul(i32 a, i32 b) { return a * b; }
i32 twice(i32 v) { return v * 2; }
i32 thrice(i32 v) { return v * 3; }

@noalloc
i32 inc(i32 v) { return v + 1; }

i32 half(i32 v) throws Rejected {
    if (v % 2 != 0) { throw Rejected {.code = v}; }
    return v / 2;
}

struct Counter { i32 count; };
i32 Counter::value(const Counter* this) { return this->count; }
void Counter::bump(Counter* this, i32 by) { this->count += by; }

@generic<T>
T same(T value) { return move value; }

struct Route {
    str name;
    fn(i32, i32) -> i32 apply;
};

const (fn(i32) -> i32)[2] SCALERS = {twice, thrice};

@generic<F: fn(i32) -> i32>
i32 apply_shared(F f, i32 v) { return f(v); }

@generic<F: fn once(i32) -> i32>
i32 apply_once(F f, i32 v) { return (move f).call(v); }

@generic<T>
T apply_to(fn(T) -> T f, T v) { return f(move v); }

@noalloc
i32 apply_fast(fn @noalloc (i32) -> i32 f, i32 v) { return f(v); }

fn(i32, i32) -> i32 pick(bool plus) {
    if (plus == true) { return add; }
    return sub;
}

i32 dispatch(const (dict<str, fn(i32, i32) -> i32>)* table, str name, i32 a, i32 b) {
    switch (std.dict::get(table, &name)) {
    case variant o::some(op): return (*op)(a, b);
    case variant o::none: return -1;
    }
    return -1;
}

i32 main() {
    dict<str, fn(i32, i32) -> i32> table = std.dict::create::<str, fn(i32, i32) -> i32>();
    try {
        std.dict::insert(&table, "add", add);
        std.dict::insert(&table, "sub", sub);
        std.dict::insert(&table, "mul", mul);
    } catch (std.dict::insert_error<str, fn(i32, i32) -> i32> failure) {
        return 1;
    }
    if (dispatch(&table, "mul", 6, 7) != 42 || dispatch(&table, "sub", 6, 7) != -1 ||
        dispatch(&table, "div", 6, 7) != -1) {
        return 2;
    }
    Route route = Route {.name = "sub", .apply = sub};
    if (route.apply(10, 4) != 6) { return 3; }
    route.apply = mul;
    if (route.apply(3, 4) != 12) { return 4; }
    if (pick(true)(2, 3) != 5 || pick(false)(2, 3) != -1) { return 5; }
    fn(i32, i32) -> i32 chosen = pick(true);
    if (chosen != add || chosen == sub) { return 6; }
    if (SCALERS[0](5) != 10 || SCALERS[1](5) != 15) { return 7; }
    Counter counter = Counter {.count = 1};
    fn(Counter*, i32) -> void bump = Counter::bump;
    fn(const Counter*) -> i32 read = Counter::value;
    bump(&counter, 4);
    if (read(&counter) != 5) { return 8; }
    fn(i32) -> i32 identity = same::<i32>;
    fn(i32) -> i32 doubling = twice;
    if (identity(9) != 9 || apply_shared(doubling, 3) != 6 || apply_once(doubling, 4) != 8) {
        return 9;
    }
    if (apply_to(thrice, 2) != 6 || apply_to::<i32>(same::<i32>, 5) != 5) { return 10; }
    array<fn(i32) -> i32> steps = std.array::create::<fn(i32) -> i32>();
    try {
        std.array::push(&steps, twice);
        std.array::push(&steps, thrice);
    } catch (std.array::push_error<fn(i32) -> i32> failure) {
        return 11;
    }
    i32 value = 1;
    for (usize index = 0usize; index < len(steps); index += 1usize) {
        value = steps[index](value);
    }
    if (value != 6) { return 12; }
    o<fn(i32) -> i32> maybe = o::some(twice);
    switch (maybe) {
    case variant o::some(f):
        if (f(21) != 42) { return 13; }
    case variant o::none:
        return 14;
    }
    fn(i32) -> i32 throws(Rejected) checked = half;
    i32 caught = 0;
    try {
        caught += checked(8);
        caught += checked(3);
    } catch (Rejected failure) {
        caught += failure.code * 10;
    }
    if (caught != 34) { return 15; }
    fn @noalloc (i32) -> i32 fast = inc;
    fn(i32) -> i32 plain = fast;
    if (apply_fast(fast, 1) != 2 || plain(2) != 3) { return 16; }
    const (fn(i32) -> i32)* borrowed = &doubling;
    if ((*borrowed)(8) != 16) { return 17; }
    return 0;
}
