module test.codegen.async_closure_modes;

struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }

@generic<F: fn once() -> i32 & send & unborrowed>
async i32 execute(F operation) {
    i32 result = (move operation).call();
    return result;
}

@generic<F: fn once() -> i32 & send & unborrowed>
i32 on_thread(F operation) {
    i32 result = (move operation).call();
    return result;
}

async i32 main() {
    i32 total = 10;
    fn mut i32 accumulate(i32 value) move(total) { total += value; return total; }
    i32 before = accumulate(5);
    if (before != 15) { return 1; }
    Tracked first = {.data = new i32(20)};
    fn once i32 work() move(first) { return *(first.data); }
    try {
        i32 result = await execute(move work);
        if (result != 20) { return 2; }
        i32 after = accumulate(7);
        if (after != 22 || total != 10) { return 3; }
        Tracked second = {.data = new i32(22)};
        fn once i32 thread_work() move(second) { return *(second.data); }
        std.thread::join_handle<i32> worker = std.thread::spawn(on_thread, move thread_work);
        std.thread::join_result<i32> outcome = (move worker).join();
        switch (move outcome) {
        case variant std.thread::join_result::returned(move value): i32 selected = value == 22 ? 0 : 4; return selected;
        case variant std.thread::join_result::panicked(move report): return 5;
        }
    } catch (std.async::start_error failure) { return 6; }
      catch (std.thread::thread_error failure) { return 7; }
}
