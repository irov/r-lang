module test.codegen.async_thread_scoped_exclusive;

/* R-MEM-0015 (L13.4): an async frame lends an exclusive borrow and an exclusive slice to scoped
   children; the join of the exact handle ends the first loan early. */

void bump(i32* value, i32 amount) {
    *value += amount;
}

void fill(i32[] values, i32 seed) {
    for (usize i = 0usize; i < len(values); i += 1usize) {
        values[i] = seed + (i as i32);
    }
}

protected async i32 run() throws std.thread::thread_error {
    i32 first = 1;
    i32[3] low = {0, 0, 0};
    thread_scope {
        std.thread::scoped_join_handle<void> a = std.thread::spawn_scoped(bump, &first, 5);
        std.thread::scoped_join_handle<void> c =
            std.thread::spawn_scoped(fill, low[0usize..3usize], 100);
        std.thread::join_result<void> joined = std.thread::join(move a);
        drop joined;
        first += 1;
        move c as void;
    }
    return first + low[0] + low[1] + low[2];
}

async i32 main() {
    try {
        i32 total = await run();
        return total - 310;
    } catch (std.thread::thread_error failure) {
        failure as void;
        return 9;
    } catch (std.async::start_error failure) {
        failure as void;
        return 8;
    }
}
