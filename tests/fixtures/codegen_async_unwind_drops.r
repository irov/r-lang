module test.codegen.async_unwind_drops;
import std.console;

/* L39 (Core R-ERR-0008, R-INIT-0010, R-MEM-0016): a panic that begins in a user drop at the end of
   a block or in a drop statement lets the rest of the destruction run as unwind cleanup and then
   leaves the function; the panic of a scoped thread escalates as scoped_thread_panic when its
   scope ends. */

atomic u64 trail = 0u64;

protected void mark(u64 digit) {
    u64 now = core::atomic_load(&trail, core::memory_order::relaxed);
    core::atomic_store(&trail, now * 10u64 + digit, core::memory_order::relaxed);
}

protected u64 take_trail() {
    u64 seen = core::atomic_load(&trail, core::memory_order::relaxed);
    core::atomic_store(&trail, 0u64, core::memory_order::relaxed);
    return seen;
}

struct Probe { u64 digit; };

drop(Probe* self) {
    mark(self->digit);
}

protected i32 pick(const u8[] values, usize at) {
    return values[at] as i32;
}

/* A drop that panics when its index is out of range, after it marked itself. */
struct Fragile { usize at; };

drop(Fragile* self) {
    mark(7u64);
    u8[2] values = {1u8, 2u8};
    pick(values[0usize..2usize], self->at) as void;
}

protected async i32 block_end(usize at) {
    Probe outer = {.digit = 1u64};
    {
        Probe first = {.digit = 2u64};
        Fragile fragile = {.at = at};
        Probe last = {.digit = 3u64};
    }
    mark(9u64);
    return 0;
}

protected async i32 statement(usize at) {
    Probe outer = {.digit = 1u64};
    Fragile fragile = {.at = at};
    Probe kept = {.digit = 4u64};
    drop fragile;
    mark(9u64);
    return 0;
}

/* A panic in a drop on the path of a return is noticed after the return: the async task ends
   with it, and so does the caller of the synchronous function. */
protected async i32 returning(usize at) {
    Probe outer = {.digit = 1u64};
    Fragile fragile = {.at = at};
    return 5;
}

protected i32 sync_returning(usize at) {
    Probe outer = {.digit = 1u64};
    Fragile fragile = {.at = at};
    return 5;
}

protected async i32 calling(usize at) {
    Probe caller = {.digit = 8u64};
    i32 value = sync_returning(at);
    mark(9u64);
    return value;
}

protected void child(usize at) {
    Probe inside = {.digit = 6u64};
    u8[2] values = {1u8, 2u8};
    pick(values[0usize..2usize], at) as void;
}

protected async i32 scoped(usize at) {
    Probe outer = {.digit = 1u64};
    try {
        thread_scope {
            std.thread::scoped_join_handle<void> handle = std.thread::spawn_scoped(child, at);
            move handle as void;
        }
    } catch (std.thread::thread_error failure) {
        failure as void;
        return 1;
    }
    mark(9u64);
    return 0;
}

/* 1 when the task returned; for a panic 2 for bounds, 3 for scoped_thread_panic with the
   category of the thread as its text, 9 otherwise. */
protected u32 outcome(std.thread::join_result<i32> joined) {
    switch (move joined) {
    case variant std.thread::join_result::returned(move value):
        value as void;
        return 1u32;
    case variant std.thread::join_result::panicked(move report):
        constexpr str category = std.thread::panic_category(&report);
        if (std.bytes::equal(category, "bounds") == true) { return 2u32; }
        if (std.bytes::equal(category, "scoped_thread_panic") == false) { return 9u32; }
        str text = std.thread::panic_text(&report);
        if (std.bytes::equal(text, "bounds") == true) { return 3u32; }
        return 9u32;
    }
}

async i32 main() {
    auto a = block_end(5usize);
    u32 first = outcome(await std.async::join(move a));
    u64 first_trail = take_trail();
    auto b = block_end(1usize);
    u32 second = outcome(await std.async::join(move b));
    u64 second_trail = take_trail();
    auto c = statement(5usize);
    u32 third = outcome(await std.async::join(move c));
    u64 third_trail = take_trail();
    auto d = scoped(5usize);
    u32 fourth = outcome(await std.async::join(move d));
    u64 fourth_trail = take_trail();
    auto e = returning(5usize);
    u32 fifth = outcome(await std.async::join(move e));
    u64 fifth_trail = take_trail();
    auto g = calling(5usize);
    u32 sixth = outcome(await std.async::join(move g));
    u64 sixth_trail = take_trail();
    await std.console::print(
        f"block {first} {first_trail}\nblock {second} {second_trail}\nstatement {third} {third_trail}\nscope {fourth} {fourth_trail}\nreturn {fifth} {fifth_trail}\ncaller {sixth} {sixth_trail}\n");
    return 0;
}
