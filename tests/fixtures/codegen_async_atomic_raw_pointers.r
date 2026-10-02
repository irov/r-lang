module test.codegen.async_atomic_raw_pointers;

/* R-TYPE-0013, R-LIB-0011: `atomic raw T*?` in an async frame, live across await: a local, a
   field, a module object, a fixed array, a variant payload, an arc payload, a generic argument
   and an async result, with load, store, exchange and compare-exchange. */

struct Point {
    i32 x;
    i32 y;
};

struct Slot {
    atomic raw const Point*? shape;
    u32 version;
};

struct Board {
    atomic raw i32*? latest;
};

enum Holder {
    Empty,
    Full(atomic raw i32*?),
};

@generic<T>
struct Wrap {
    T value;
};

i32 origin_x = 5;
atomic raw i32*? global_cell = null;

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async atomic raw i32*? fresh() {
    atomic raw i32*? slot = null;
    return move slot;
}

protected async i32 operations(i32 seed) throws std.async::start_error {
    Point point = Point {.x = seed, .y = 4};
    atomic raw i32*? slot = null;
    Slot holder = Slot {.shape = null, .version = 1u32};
    i32 value = await tick(seed);
    unsafe {
        raw i32* target = &value as raw i32*;
        raw i32*? previous = core::atomic_exchange(&slot, target, core::memory_order::acq_rel);
        if (previous != null) { return 1; }
        core::atomic_store(&holder.shape, &point as raw const Point*, core::memory_order::release);
    }
    i32 later = await tick(value);
    if (later != seed + 2) { return 2; }
    unsafe {
        raw i32*? loaded = core::atomic_load(&slot, core::memory_order::acquire);
        if (loaded == null) { return 3; }
        if (*loaded != seed + 1) { return 4; }
        core::atomic_compare_exchange_result<raw i32*?> outcome = core::atomic_compare_exchange(
            &slot, loaded, null, core::memory_order::seq_cst, core::memory_order::relaxed);
        switch (move outcome) {
            case variant core::atomic_compare_exchange_result::exchanged(move observed):
                if (observed == null) { return 5; }
                break;
            case variant core::atomic_compare_exchange_result::unchanged(move observed):
                observed as void;
                return 6;
        }
        raw const Point*? shape = core::atomic_load(&holder.shape, core::memory_order::acquire);
        if (shape == null) { return 7; }
        if ((*shape).x != seed) { return 8; }
        raw i32* cell = &origin_x as raw i32*;
        core::atomic_store(&global_cell, cell, core::memory_order::release);
        raw i32*? published = core::atomic_load(&global_cell, core::memory_order::acquire);
        if (published == null) { return 9; }
        if (*published != 5) { return 10; }
    }
    return 0;
}

protected async i32 positions() throws std.async::start_error {
    i32 value = 1;
    atomic raw i32*? made = await fresh();
    unsafe {
        if (core::atomic_load(&made, core::memory_order::relaxed) != null) { return 20; }
    }
    (atomic raw i32*?)[2] pair = {null, null};
    Holder holder = Holder::Full(null);
    arc Board shared = new arc Board {.latest = null};
    Wrap<atomic raw i32*?> wrapped = Wrap<atomic raw i32*?> {.value = null};
    i32 next = await tick(value);
    if (next != 2) { return 21; }
    unsafe {
        core::atomic_store(&pair[1], &value as raw i32*, core::memory_order::relaxed);
        if (core::atomic_load(&pair[1], core::memory_order::relaxed) == null) { return 22; }
        core::atomic_store(&shared->latest, &value as raw i32*, core::memory_order::release);
        if (core::atomic_load(&shared->latest, core::memory_order::acquire) == null) { return 23; }
        core::atomic_store(&wrapped.value, &value as raw i32*, core::memory_order::relaxed);
        if (core::atomic_load(&wrapped.value, core::memory_order::relaxed) == null) { return 24; }
    }
    switch (holder) {
        case variant Holder::Full(slot):
            unsafe {
                core::atomic_store(slot, &value as raw i32*, core::memory_order::relaxed);
                if (core::atomic_load(slot, core::memory_order::relaxed) == null) { return 25; }
            }
            break;
        case variant Holder::Empty:
            return 26;
    }
    return 0;
}

async i32 main() {
    try {
        const i32 status = await operations(3);
        if (status != 0) { return status; }
        return await positions();
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
