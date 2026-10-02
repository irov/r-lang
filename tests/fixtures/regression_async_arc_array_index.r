module test.regression.async_arc_array_index;

/* M26-4: in an async function an array<T> field indexed through an arc was written as a fixed
   array when its statement was broken over lines. */

struct Holder { array<u32> items; };

protected array<u32> make() {
    array<u32> items = std.array::create::<u32>();
    try {
        items.push(3u32);
        items.push(4u32);
    } catch (std.array::push_error<u32> rejected) {
        rejected as void;
    }
    return move items;
}

u32 sync_sum(arc Holder state) {
    u32 total = 0u32;
    for (usize index = 0usize; index < len(state->items); index += 1usize) { total += state->items[index]; }
    return total;
}

async u32 async_sum(arc Holder state) {
    u32 total = 0u32;
    for (usize index = 0usize; index < len(state->items); index += 1usize) { total += state->items[index]; }
    return total;
}

async i32 main() {
    arc Holder state = new arc Holder {.items = make()};
    u32 first = sync_sum(std.arc::clone(&state));
    u32 second = await async_sum(move state);
    u32 total = first * 100u32 + second;
    if (total != 707u32) { return 1; }
    return 0;
}
