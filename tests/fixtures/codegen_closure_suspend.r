module test.codegen.closure_suspend;

// A fn once capture held by an async frame across a suspended await (Core R-FUNC-0017): kept
// while the frame waits, consumed by the call after it resumes, destroyed by the frame when it
// is cancelled while it waits. tests/codegen_closure_suspend_wrapper.c opens `ready` once the
// frame has suspended and checks the released values (1007 consumed, 1022 destroyed).
struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) += 1000; }

@generic<F: fn once() -> i32 & send & unborrowed>
async i32 execute(F operation, task<i32> waiting) {
    i32 prior = await move waiting;
    i32 value = (move operation).call();
    return prior + value;
}

async i32 number(std.async::notify gate) {
    try { await gate.notified(); } catch (std.async::start_error failure) { return 0; }
    return 20;
}

async i32 main() {
    try {
        std.async::notify gate = std.async::notify_new();
        std.async::notify ready = std.async::notify_new();
        Tracked value = {.data = new i32(22)};
        fn once i32 work() move(value) {
            i32 read = *(value.data);
            *(value.data) = 7;
            return read;
        }
        task<i32> pending = number(gate.clone());
        task<i32> running = execute(move work, move pending);
        await ready.notified();
        gate.notify_one();
        i32 result = await move running;
        if (result != 42) { return 1; }
    } catch (std.async::start_error failure) { return 2; }
      catch (std.alloc::alloc_error failure) { return 3; }
    try {
        std.async::notify gate = std.async::notify_new();
        std.async::notify ready = std.async::notify_new();
        Tracked value = {.data = new i32(22)};
        fn once i32 work() move(value) {
            i32 read = *(value.data);
            *(value.data) = 7;
            return read;
        }
        task<i32> pending = number(gate.clone());
        task<i32> running = execute(move work, move pending);
        await ready.notified();
        std.async::cancel(move running);
    } catch (std.async::start_error failure) { return 4; }
      catch (std.alloc::alloc_error failure) { return 5; }
    return 0;
}
