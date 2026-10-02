module test.codegen.thread_scoped_exclusive;

/* R-MEM-0015 (L13.4): scoped children take exclusive borrows and exclusive slices by value.
   Each is a loan of the thread region that suspends the parent's access to its storage until
   region completion, or until the join of the exact handle, after which the parent writes
   again; a named exclusive borrow is lent the same way. */

void bump(i32* value, i32 amount) {
    *value += amount;
}

void fill(i32[] values, i32 seed) {
    for (usize i = 0usize; i < len(values); i += 1usize) {
        values[i] = seed + (i as i32);
    }
}

i32 count(const i32[] values) {
    i32 total = 0;
    for (usize i = 0usize; i < len(values); i += 1usize) {
        total += values[i];
    }
    return total;
}

i32 run() throws std.thread::thread_error {
    i32 first = 1;
    i32 second = 10;
    i32[3] low = {0, 0, 0};
    i32[3] high = {0, 0, 0};
    i32* named = &second;
    thread_scope {
        std.thread::scoped_join_handle<void> a = std.thread::spawn_scoped(bump, &first, 5);
        std.thread::scoped_join_handle<void> b = std.thread::spawn_scoped(bump, named, 7);
        std.thread::scoped_join_handle<void> c =
            std.thread::spawn_scoped(fill, low[0usize..3usize], 100);
        std.thread::scoped_join_handle<void> d =
            std.thread::spawn_scoped(fill, high[0usize..3usize], 200);
        std.thread::join_result<void> joined = std.thread::join(move a);
        drop joined;
        first += 1;
        move b as void;
        move c as void;
        move d as void;
    }
    return first + second + count(low[0usize..3usize]) + count(high[0usize..3usize]);
}

i32 main() {
    try {
        i32 total = run();
        return total - 930;
    } catch (std.thread::thread_error failure) {
        failure as void;
        return 9;
    }
}
