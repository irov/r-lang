module test.regression.container_helper_kinds;

/* M26-3: arrays of two different function types, both represented as u32, share one push
   helper; it was emitted twice. */
protected u32 first(u32 value) { return value + 1u32; }
protected bool second(u32 value, u32 other) { return value == other; }

protected array<fn(u32) -> u32> make_steps() {
    array<fn(u32) -> u32> steps = std.array::create::<fn(u32) -> u32>();
    try {
        steps.push(first);
    } catch (std.array::push_error<fn(u32) -> u32> rejected) {
        rejected as void;
    }
    return move steps;
}

protected array<fn(u32, u32) -> bool> make_tests() {
    array<fn(u32, u32) -> bool> tests = std.array::create::<fn(u32, u32) -> bool>();
    try {
        tests.push(second);
    } catch (std.array::push_error<fn(u32, u32) -> bool> rejected) {
        rejected as void;
    }
    return move tests;
}

i32 main() {
    array<fn(u32) -> u32> steps = make_steps();
    array<fn(u32, u32) -> bool> tests = make_tests();
    u32 result = steps[0usize](1u32);
    bool same = tests[0usize](result, 2u32);
    if (same == false) { return 1; }
    return 0;
}
