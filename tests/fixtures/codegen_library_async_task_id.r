module test.codegen.library_async_task_id;

// R-SLIB-ASYNC-0018 (M23): the task of an asynchronous main is the first task, and every
// task started later, by an awaited call, in a group or on the blocking call pool, has a
// larger identifier in start order; the identifier of a task does not change.

struct Ids { u64 first; u64 second; u64 pooled; };

u64 pooled(u64 unused) {
    unused as void;
    return std.async::task_id();
}

async u64 current() {
    return std.async::task_id();
}

async i32 main() {
    i32 status = 0;
    u64 main_id = std.async::task_id();
    if (main_id != 1u64) { status += 1; }
    u64 awaited = await current();
    if (awaited <= main_id) { status += 2; }
    Ids ids = {.first = 0u64, .second = 0u64, .pooled = 0u64};
    try {
        task_scope(2) group {
            task<u64> first = current();
            task<u64> second = current();
            ids.first = await move first;
            ids.second = await move second;
        }
        ids.pooled = await std.async::blocking(pooled, 0u64);
    } catch (std.async::start_error failure) {
        failure as void;
        status += 4;
    }
    if (ids.first <= awaited) { status += 8; }
    if (ids.second <= ids.first) { status += 16; }
    if (ids.pooled <= ids.second) { status += 32; }
    if (std.async::task_id() != main_id) { status += 64; }
    return status;
}
