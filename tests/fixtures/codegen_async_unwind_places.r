module test.codegen.async_unwind_places;
import std.console;

/* L39-12 (Core R-ERR-0005): the bounds check of an indexed place that a statement prepares outside
   an expression, the operand of return and the destination of an assignment or a compound
   assignment, begins a panic that unwinds like any other. */

protected u8 pick(const u8[] values, usize at) {
    return values[at];
}

protected void store(u8[] values, usize at) {
    values[at] = 9u8;
}

protected void bump(u8[] values, usize at) {
    values[at] += 1u8;
}

protected async u32 run(u32 mode, usize at) {
    u8[3] values = {1u8, 2u8, 3u8};
    switch (mode) {
    case 0u32: return pick(values[0usize..3usize], at) as u32;
    case 1u32: store(values[0usize..3usize], at);
    default: bump(values[0usize..3usize], at);
    }
    return values[0usize] as u32 + values[1usize] as u32 + values[2usize] as u32;
}

protected str outcome(std.thread::join_result<u32> joined) {
    switch (move joined) {
    case variant std.thread::join_result::returned(move value):
        value as void;
        return "returned";
    case variant std.thread::join_result::panicked(move report):
        constexpr str category = std.thread::panic_category(&report);
        return category;
    }
}

async i32 main() {
    i32 failures = 0;
    for (u32 mode = 0u32; mode < 3u32; mode += 1u32) {
        auto inside = run(mode, 1usize);
        std.thread::join_result<u32> fine = await std.async::join(move inside);
        if (std.bytes::equal(outcome(move fine), "returned") == false) { failures += 1; }
        auto outside = run(mode, 7usize);
        std.thread::join_result<u32> broken = await std.async::join(move outside);
        if (std.bytes::equal(outcome(move broken), "bounds") == false) { failures += 10; }
    }
    await std.console::println(std.string::from_str("places unwind"));
    return failures;
}
