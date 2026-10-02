module test.regression.async_aggregate_argument;

/* M27-1: in async code after an await, a struct literal holding a moved local passed straight
   to a synchronous call was rejected as not lowerable. */

struct Pair { std.string::string name; u32 value; };

@generic<T>
protected void append(array<T>* target, T value) throws std.alloc::alloc_error {
    try {
        target->push(move value);
    } catch (std.array::push_error<T> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}


protected u32 take(Pair pair) { return pair.value; }

protected async u32 plain_call() throws std.error::fault {
    return take(Pair {.name = std.string::from_str("a"), .value = 3u32});
}

protected async usize generic_call() throws std.error::fault {
    u32 base = await plain_call();
    array<Pair> items = std.array::create::<Pair>();
    for (u32 index = 0u32; index < base; index += 1u32) {
        std.string::string name = std.string::from_str("b");
        append(&items, Pair {.name = move name, .value = index});
    }
    return len(items);
}

async i32 main() {
    u32 first = await plain_call();
    try {
        usize second = await generic_call();
        return (first as i32) + (second as i32) - 6;
    } catch (std.error::fault failed) {
        failed as void;
        return 1;
    }
}
