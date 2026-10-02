module test.codegen.async_closure_start_recovery;

struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }

@generic<F: fn once() -> i32 & send & unborrowed>
i32 on_thread(F operation) {
    i32 result = (move operation).call();
    return result;
}

async i32 main() {
    try {
        Tracked first = {.data = new i32(20)};
        async fn i32 work() move(first) { return *(first.data); }
        i32 finalized = 0;
        try {
            i32 unexpected = await (move work).call();
            throw TestAssertionFailed {.code = 1};
        } catch (std.async::start_error failure) {
            finalized += 1;
        } finally { finalized += 10; }
        if (finalized != 11) { throw TestAssertionFailed {.code = 2}; }
        try {
            i32 result = await (move work).call();
            if (result != 20) { throw TestAssertionFailed {.code = 3}; }
            Tracked second = {.data = new i32(22)};
            fn once i32 thread_work() move(second) { return *(second.data); }
            try {
                std.thread::join_handle<i32> unexpected = std.thread::spawn(on_thread, move thread_work);
                std.thread::join_result<i32> ignored = (move unexpected).join();
                throw TestAssertionFailed {.code = 4};
            } catch (std.thread::thread_error failure) { finalized += 1; }
              finally { finalized += 10; }
            if (finalized != 22) { throw TestAssertionFailed {.code = 5}; }
            std.thread::join_handle<i32> worker = std.thread::spawn(on_thread, move thread_work);
            std.thread::join_result<i32> outcome = (move worker).join();
            switch (move outcome) {
            case variant std.thread::join_result::returned(move value): i32 selected = value == 22 ? 0 : 6; return selected;
            case variant std.thread::join_result::panicked(move report): throw TestAssertionFailed {.code = 7};
            }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 8}; }
          catch (std.thread::thread_error failure) { throw TestAssertionFailed {.code = 9}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
