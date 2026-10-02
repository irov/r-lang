module test.scoped_native;
async i32 main() {
    try {
        try {
            std.time::duration brief = std.time::duration_from_parts(0i64, 2000000u32);
            task_scope(2) timers {
                auto first = brief.sleep_for();
                auto second = brief.sleep_for();
                usize selected = await timers.first(&first, &second);
                if (selected > 1usize) { throw TestAssertionFailed {.code = 1}; }
                await timers.all();
                await move first;
                await move second;
            }
            std.time::instant started = std.time::monotonic_now();
            std.time::duration long_delay = std.time::duration_from_seconds(60i64);
            task_scope(1) cancelled { auto pending = long_delay.sleep_for(); std.async::cancel(move pending); }
            std.time::instant finished = std.time::monotonic_now();
            std.time::duration elapsed = finished.duration(started);
            i32 chosen = elapsed.seconds() < 10i64 ? 0 : 2;
            return chosen;
        } catch (std.time::time_error failure) { throw TestAssertionFailed {.code = 3}; }
        catch (std.time::duration_error failure) { throw TestAssertionFailed {.code = 4}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 5}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
