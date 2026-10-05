module test.codegen.async_arc_last_release;

/* M44-1: a value shared by arc is read on one thread, which then drops its owner without any
   other synchronization with the task that holds the last one; that task drops the value on
   another thread. The last release orders every earlier access before the drop. ThreadSanitizer,
   which models no standalone fence, reported a release decrement followed by an acquire fence
   in this order as a race. */

struct State {
    std.string::string name;
    array<u32> values;
};

atomic u32 finished = 0u32;
atomic u32 summed = 0u32;

/* Holds the last owner past a pause, so that the reader has dropped its own by then. */
async void hold(arc State state) {
    try {
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 2000000u32));
    } catch (std.error::fault failure) {
        failure as void;
    }
    u32 total = 0u32;
    for (usize index = 0usize; index < len(state->values); index += 1usize) { total += state->values[index]; }
    drop state;
    core::atomic_fetch_add(&summed, total, core::memory_order::relaxed) as void;
    core::atomic_fetch_add(&finished, 1u32, core::memory_order::release) as void;
}

/* Thirty-two readers in turn: after its read and its drop each allocates nothing and waits on
   the flag alone, so only the counter of the arc orders its read before the drop on the other
   thread. */
async i32 rounds() throws std.error::fault {
    for (u32 round = 0u32; round < 32u32; round += 1u32) {
        array<u32> values = std.array::filled(2usize, 3u32);
        arc State state = new arc State {.name = std.string::from_str("shared"), .values = move values};
        task<void> worker = hold(std.arc::clone(&state));
        (move worker).detach();
        u32 seen = std.string::len(&state->name) as u32;
        drop state;
        while (core::atomic_load(&finished, core::memory_order::acquire) == round) {}
        if (seen != 6u32) { return 1; }
    }
    if (core::atomic_load(&summed, core::memory_order::relaxed) != 32u32 * 6u32) { return 2; }
    return 0;
}

async i32 main() {
    try {
        return await rounds();
    } catch (std.error::fault failure) {
        failure as void;
    }
    return 3;
}
