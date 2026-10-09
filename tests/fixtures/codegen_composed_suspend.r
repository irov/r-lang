module test.codegen.composed_suspend;

// A temporary owner evaluated before an await in the same call (Core R-EXPR-0020) is held by
// the frame across the suspension: kept while the frame waits, consumed by the call after it
// resumes, destroyed with the frame when it is cancelled while it waits.
// tests/codegen_closure_suspend_wrapper.c opens `ready` once the frame has suspended and checks
// the released values (1007 consumed, 1022 destroyed unconsumed).
struct Owner { own i32* value; };
drop(Owner* self) { *(self->value) += 1000; }
Owner create(i32 value) { return Owner {.value = new i32(value)}; }
i32 consume(Owner value, i32 addition) {
    i32 read = *value.value;
    *value.value = 7;
    return read + addition;
}

async i32 evaluate(task<i32> pending) {
    return consume(create(22), await move pending);
}

async i32 child(std.async::notify gate) {
    try { await gate.notified(); } catch (std.async::start_error failure) { return 0; }
    return 20;
}

async i32 main() {
    try {
        std.async::notify gate = std.async::notify_new();
        std.async::notify ready = std.async::notify_new();
        task<i32> pending = child(gate.clone());
        task<i32> running = evaluate(move pending);
        await ready.notified();
        gate.notify_one();
        i32 result = await move running;
        if (result != 42) { return 1; }
    } catch (std.async::start_error failure) { return 2; }
      catch (std.alloc::alloc_error failure) { return 3; }
    try {
        std.async::notify gate = std.async::notify_new();
        std.async::notify ready = std.async::notify_new();
        task<i32> pending = child(gate.clone());
        task<i32> running = evaluate(move pending);
        await ready.notified();
        std.async::cancel(move running);
    } catch (std.async::start_error failure) { return 4; }
      catch (std.alloc::alloc_error failure) { return 5; }
    return 0;
}
