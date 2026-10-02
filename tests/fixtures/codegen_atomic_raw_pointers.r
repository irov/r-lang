module test.codegen.atomic_raw_pointers;

/* R-TYPE-0013, R-LIB-0011: `atomic raw T*?` holds a nullable raw object pointer in a local, a
   field, a module object, a thread-local instance, a fixed array, a variant payload, an arc
   payload, a generic argument, a container element and a by-value parameter or result. Load,
   store, exchange and compare-exchange are its operations; scoped threads publish pointers
   through one shared atomic. */

struct Point {
    i32 x;
    i32 y;
};

struct Slot {
    atomic raw i32*? current;
    atomic raw const Point*? shape;
    u32 version;
};

struct Board {
    atomic raw i32*? latest;
    au32 swaps;
};

enum Holder {
    Empty,
    Full(atomic raw i32*?),
};

@generic<T>
struct Cell {
    atomic raw T*? head;
};

@generic<T>
struct Wrap {
    T value;
};

i32 first_cell = 11;
atomic raw void*? global_slot = null;
thread_local atomic raw i32*? local_cell = null;

atomic raw i32*? fresh() {
    atomic raw i32*? slot = null;
    return move slot;
}

bool is_empty(atomic raw i32*? slot) {
    unsafe {
        return core::atomic_load(&slot, core::memory_order::relaxed) == null;
    }
}

@generic<T>
bool cell_empty(const Cell<T>* cell) {
    unsafe {
        return core::atomic_load(&cell->head, core::memory_order::acquire) == null;
    }
}

i32 operations() {
    i32 value = 7;
    i32 other = 9;
    Point point = Point {.x = 3, .y = 4};
    atomic raw i32*? slot = null;
    unsafe {
        raw i32* target = &value as raw i32*;
        raw i32*? previous = core::atomic_exchange(&slot, target, core::memory_order::acq_rel);
        if (previous != null) { return 1; }
        raw i32*? loaded = core::atomic_load(&slot, core::memory_order::acquire);
        if (loaded == null) { return 2; }
        if (*loaded != 7) { return 3; }
        core::atomic_store(&slot, &other as raw i32*, core::memory_order::release);
        core::atomic_compare_exchange_result<raw i32*?> missed = core::atomic_compare_exchange(
            &slot, target, null, core::memory_order::seq_cst, core::memory_order::relaxed);
        switch (move missed) {
            case variant core::atomic_compare_exchange_result::exchanged(move observed):
                observed as void;
                return 4;
            case variant core::atomic_compare_exchange_result::unchanged(move observed):
                if (observed == null) { return 5; }
                if (*observed != 9) { return 6; }
                break;
        }
        raw i32*? current = core::atomic_load(&slot, core::memory_order::acquire);
        core::atomic_compare_exchange_result<raw i32*?> hit = core::atomic_compare_exchange(
            &slot, current, null, core::memory_order::acq_rel, core::memory_order::acquire);
        switch (move hit) {
            case variant core::atomic_compare_exchange_result::exchanged(move observed):
                if (observed == null) { return 7; }
                break;
            case variant core::atomic_compare_exchange_result::unchanged(move observed):
                observed as void;
                return 8;
        }
        if (core::atomic_load(&slot, core::memory_order::relaxed) != null) { return 9; }
        Slot holder = Slot {.current = null, .shape = &point as raw const Point*, .version = 1u32};
        raw const Point*? shape = core::atomic_load(&holder.shape, core::memory_order::relaxed);
        if (shape == null) { return 10; }
        if ((*shape).y != 4) { return 11; }
        raw i32* cell = &first_cell as raw i32*;
        core::atomic_store(&global_slot, cell as raw void*, core::memory_order::release);
        if (core::atomic_load(&global_slot, core::memory_order::acquire) == null) { return 12; }
    }
    core::atomic_is_lock_free(&slot) as void;
    return 0;
}

i32 positions() throws std.array::push_error<atomic raw i32*?> {
    i32 value = 1;
    atomic raw i32*? made = fresh();
    if (is_empty(move made) == false) { return 20; }
    (atomic raw i32*?)[2] pair = {null, null};
    Holder holder = Holder::Full(null);
    arc Board shared = new arc Board {.latest = null, .swaps = 0u32};
    Wrap<atomic raw i32*?> wrapped = Wrap<atomic raw i32*?> {.value = null};
    Cell<Point> points = Cell<Point> {.head = null};
    array<atomic raw i32*?> items = std.array::create::<atomic raw i32*?>();
    std.array::push(&items, null);
    unsafe {
        core::atomic_store(&pair[1], &value as raw i32*, core::memory_order::relaxed);
        if (core::atomic_load(&pair[1], core::memory_order::relaxed) == null) { return 21; }
        if (core::atomic_load(&pair[0], core::memory_order::relaxed) != null) { return 22; }
        core::atomic_store(&shared->latest, &value as raw i32*, core::memory_order::release);
        if (core::atomic_load(&shared->latest, core::memory_order::acquire) == null) { return 23; }
        core::atomic_store(&local_cell, &value as raw i32*, core::memory_order::relaxed);
        if (core::atomic_load(&local_cell, core::memory_order::relaxed) == null) { return 24; }
        core::atomic_store(&wrapped.value, &value as raw i32*, core::memory_order::relaxed);
        if (core::atomic_load(&wrapped.value, core::memory_order::relaxed) == null) { return 25; }
        core::atomic_store(&items[0], &value as raw i32*, core::memory_order::relaxed);
        if (core::atomic_load(&items[0], core::memory_order::relaxed) == null) { return 26; }
    }
    switch (holder) {
        case variant Holder::Full(slot):
            unsafe {
                core::atomic_store(slot, &value as raw i32*, core::memory_order::relaxed);
                if (core::atomic_load(slot, core::memory_order::relaxed) == null) { return 27; }
            }
            break;
        case variant Holder::Empty:
            return 28;
    }
    if (cell_empty(&points) == false) { return 29; }
    constexpr str name = core::type_name::<atomic raw const i32*?>();
    if (len(name) != 22usize) { return 30; }
    if (sizeof(atomic raw i32*?) != sizeof(raw i32*?)) { return 31; }
    return 0;
}

void publish(const Board* board, i32* cell) {
    unsafe {
        raw i32* mine = cell as raw i32*;
        raw i32*? seen = core::atomic_load(&board->latest, core::memory_order::relaxed);
        while (true) {
            core::atomic_compare_exchange_result<raw i32*?> outcome = core::atomic_compare_exchange(
                &board->latest, seen, mine, core::memory_order::acq_rel, core::memory_order::acquire);
            bool exchanged = false;
            switch (move outcome) {
                case variant core::atomic_compare_exchange_result::exchanged(move observed):
                    observed as void;
                    exchanged = true;
                    break;
                case variant core::atomic_compare_exchange_result::unchanged(move observed):
                    seen = observed;
                    break;
            }
            if (exchanged == true) {
                break;
            }
        }
    }
    core::atomic_fetch_add(&board->swaps, 1u32, core::memory_order::relaxed) as void;
}

i32 threads() throws std.thread::thread_error {
    Board board = Board {.latest = null, .swaps = 0u32};
    i32 a = 1;
    i32 b = 2;
    i32 c = 3;
    i32 d = 4;
    thread_scope {
        std.thread::scoped_join_handle<void> ha = std.thread::spawn_scoped(publish, &board, &a);
        std.thread::scoped_join_handle<void> hb = std.thread::spawn_scoped(publish, &board, &b);
        std.thread::scoped_join_handle<void> hc = std.thread::spawn_scoped(publish, &board, &c);
        std.thread::scoped_join_handle<void> hd = std.thread::spawn_scoped(publish, &board, &d);
        move ha as void;
        move hb as void;
        move hc as void;
        move hd as void;
    }
    if (core::atomic_load(&board.swaps, core::memory_order::acquire) != 4u32) { return 40; }
    unsafe {
        raw i32*? last = core::atomic_load(&board.latest, core::memory_order::acquire);
        if (last == null) { return 41; }
        if (*last < 1 || *last > 4) { return 42; }
    }
    return 0;
}

i32 main() {
    const i32 status = operations();
    if (status != 0) { return status; }
    try {
        const i32 placed = positions();
        if (placed != 0) { return placed; }
        return threads();
    } catch (std.array::push_error<atomic raw i32*?> failure) {
        move failure as void;
        return 50;
    } catch (std.thread::thread_error failure) {
        failure as void;
        return 51;
    }
}
