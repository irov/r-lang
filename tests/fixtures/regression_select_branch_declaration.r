module test.regression.select_branch_declaration;

/* M32T-2: the head of a select branch is its first node after case; a branch `await move
   member` whose body declares a local is not taken for a branch of the form `T name = await
   move member` (R-STMT-0018). */
protected async u32 quick(u32 value) { return value; }

protected async void slow() throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_seconds(30i64));
}

async i32 main() {
    u32 got = 0u32;
    task_scope(2) group {
        auto first = quick(7u32);
        auto second = slow();
        select (group) {
        case await move second:
            u32 late = 1u32;
            got = late;
        case u32 value = await move first:
            u32 doubled = value * 2u32;
            got = doubled;
        }
        group.cancel_all();
    }
    if (got == 14u32) { return 0; }
    return 1;
}
