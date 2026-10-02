module test.regression.fn_value_unborrowed;

/* M26-2: a function value holds no view whatever its parameters borrow, so a generic owner of
   function values with borrowed parameters is unborrowed and may cross an async frame. */
@generic<S: send & sync & unborrowed>
struct hooks { array<fn(const S*) -> u32> checks; };

protected u32 read_value(const u32* value) { return *value; }

@generic<S: send & sync & unborrowed>
protected async u32 run_hooks(arc hooks<S> owner, arc S state) {
    u32 total = 0u32;
    for (usize index = 0usize; index < len(owner->checks); index += 1usize) {
        auto check = owner->checks[index];
        total += check(&*state);
    }
    return total;
}

async i32 main() {
    array<fn(const u32*) -> u32> checks = std.array::create::<fn(const u32*) -> u32>();
    try {
        checks.push(read_value);
        checks.push(read_value);
    } catch (std.array::push_error<fn(const u32*) -> u32> rejected) {
        rejected as void;
        return 2;
    }
    arc hooks<u32> owner = new arc hooks<u32> {.checks = move checks};
    arc u32 state = new arc u32(21u32);
    u32 total = await run_hooks(move owner, move state);
    return (total as i32) - 42;
}
