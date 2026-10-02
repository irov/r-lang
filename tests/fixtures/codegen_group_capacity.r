module test.codegen.group_capacity;

/* R-STMT-0017 (L25.2): the capacity of a task group may be spelled over the constant generic
   parameters of its function, directly or through a constant local; each instance has a group of
   its own size, and a start beyond it fails with scope_full. */

async void rest() throws std.time::time_error, std.time::duration_error, std.async::start_error {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 100000000u32));
}

@generic<const usize N>
async usize admitted() throws std.async::start_error {
    usize count = 0usize;
    task_scope(N) group {
        try {
            for (usize index = 0usize; index <= N; index += 1usize) {
                auto member = rest();
                std.async::detach(move member);
                count += 1usize;
            }
        } catch (std.async::start_error failure) {
            failure as void;
        }
        await group.all();
    }
    return count;
}

@generic<const usize N>
async usize doubled() throws std.async::start_error {
    const usize size = N * 2usize;
    usize count = 0usize;
    task_scope(size) group {
        try {
            for (usize index = 0usize; index <= size; index += 1usize) {
                auto member = rest();
                std.async::detach(move member);
                count += 1usize;
            }
        } catch (std.async::start_error failure) {
            failure as void;
        }
        await group.all();
    }
    return count;
}

async i32 main() {
    i32 status = 0;
    try {
        if (await admitted::<1usize>() != 1usize) { status += 1; }
        if (await admitted::<3usize>() != 3usize) { status += 2; }
        if (await doubled::<2usize>() != 4usize) { status += 4; }
    } catch (std.async::start_error failure) {
        status += 8;
    }
    return status;
}
