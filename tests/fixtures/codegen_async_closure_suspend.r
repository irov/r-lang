module test.codegen.async_closure_suspend;

// The capture of an async closure held by its frame across a suspended await (Core
// R-FUNC-0017): kept while the frame waits, read after it resumes, destroyed with the frame when
// it is cancelled while it waits. tests/codegen_closure_suspend_wrapper.c opens `ready` once the
// frame has suspended and checks the released values (1007 read, 1022 destroyed unread).
struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) += 1000; }

async i32 number(std.async::notify gate) {
    try { await gate.notified(); } catch (std.async::start_error failure) { return 0; }
    return 20;
}

async i32 main() {
    try {
        std.async::notify gate = std.async::notify_new();
        std.async::notify ready = std.async::notify_new();
        Tracked value = {.data = new i32(22)};
        async fn i32 work(task<i32> waiting) move(value) {
            i32 prior = await move waiting;
            i32 read = *(value.data);
            *(value.data) = 7;
            return prior + read;
        }
        task<i32> pending = number(gate.clone());
        task<i32> running = (move work).call(move pending);
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
        async fn i32 work(task<i32> waiting) move(value) {
            i32 prior = await move waiting;
            i32 read = *(value.data);
            *(value.data) = 7;
            return prior + read;
        }
        task<i32> pending = number(gate.clone());
        task<i32> running = (move work).call(move pending);
        await ready.notified();
        std.async::cancel(move running);
    } catch (std.async::start_error failure) { return 4; }
      catch (std.alloc::alloc_error failure) { return 5; }
    return 0;
}
