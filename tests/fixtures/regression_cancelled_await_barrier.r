module test.regression.cancelled_await_barrier;

/* M26-6: a task that a group cancels while it awaits a member of its own scope reaches the
   barrier of that scope still registered as the waiter of the member; when the member was just
   publishing its end, the barrier must wait for the wakeup instead of failing. The race is
   narrow, so the program provokes it many times. */

protected async u32 quick() throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000u32));
    return 1u32;
}

protected async u32 keep_awaiting() throws std.error::fault {
    u32 total = 0u32;
    for (u32 index = 0u32; index < 1000000u32; index += 1u32) {
        task_scope(1) inner { total += await quick(); }
    }
    return total;
}

protected async void fail_soon() throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 2000000u32));
    u16 value = std.convert::parse_u16("x", 10u32);
    value as void;
}

async i32 main() {
    u32 failures = 0u32;
    for (u32 round = 0u32; round < 200u32; round += 1u32) {
        try {
            task_scope(2) group {
                auto busy = keep_awaiting();
                auto failing = fail_soon();
                await move failing;
                u32 total = await move busy;
                total as void;
            }
        } catch (std.error::fault rejected) {
            rejected as void;
            failures += 1u32;
        }
    }
    if (failures != 200u32) { return 1; }
    return 0;
}
