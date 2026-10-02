module test.regression.standard_argument_send;

/* M32T-1: the Send and Sync of an async fn type decide from its parameters; a parameter whose
   struct holds a standard generic type, here std.sync::mutex<array<entry>>, collects the
   aggregates of the arguments of that type when the fn type is met first, as a_holder, whose
   name comes before the others, makes it (R-TYPE-0054). */
struct a_holder { async fn(u32, call_like) -> u32 handler; };
struct entry { std.string::string id; };
struct board { std.sync::mutex<array<entry>> items; };
struct call_like { board inner; };

protected async u32 use_call(u32 value, call_like given) {
    drop given;
    return value + 1u32;
}

async i32 main() {
    a_holder kept = {.handler = use_call};
    auto handler = kept.handler;
    call_like given = {.inner = board {.items = std.sync::mutex_new(std.array::create::<entry>())}};
    u32 got = await handler(41u32, move given);
    if (got == 42u32) { return 0; }
    return 1;
}
