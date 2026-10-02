module test.regression.select_branch_pending;

/* M32-5: a clause of select awaits another listed member on one branch only; that member is
   select-pending afterwards and cancel_all resolves it (R-STMT-0018). */
protected async u32 quick(u32 value) { return value; }

protected async u32 slow() throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_seconds(30i64));
    return 2u32;
}

async i32 main() {
    u32 got = 0u32;
    task_scope(2) group {
        auto first = quick(1u32);
        auto second = slow();
        select (group) {
        case u32 value = await move first:
            if (value == 1u32) {
                got = value;
            } else {
                u32 other = await move second;
                got = other;
            }
        case u32 value = await move second: got = value;
        }
        group.cancel_all();
    }
    if (got == 1u32) { return 0; }
    return 1;
}
