module test.codegen.async_borrow_calls;

/* R-BORROW-0024, R-FUNC-0022, R-STMT-0017: synchronous calls inside async frames take borrows,
   out destinations and scoped views, also when another argument is a nested call. */
struct Counter { atomic u32 drops; };
struct Tracked { arc Counter counter; i32 value; };
drop(Tracked* self) {
    const Counter* c = &*self->counter;
    core::atomic_fetch_add(&c->drops, 1u32, core::memory_order::relaxed) as void;
}
error Failed { i32 code; };
struct Box { i32 value; };

i32 twice(i32 value) { return value * 2; }
i32 add_to(i32* target, i32 amount) {
    *target += amount;
    return *target;
}
i32 read_from(const i32* source, i32 amount) { return *source + amount; }
i32 Box::add(Box* this, i32 amount) {
    this->value += amount;
    return this->value;
}
void put_first(i32[] values, i32 value) { values[0] = value; }
void make(out Tracked result, arc Counter counter, i32 value) throws Failed {
    if (value < 0) { throw Failed {.code = value}; }
    result = Tracked {.counter = move counter, .value = value};
}
void make_number(out i32 result, i32 value) { result = value; }

async void pause() {}

@scoped
async i32 worker(i32* total, const i32* shared, i32[] window) throws std.async::start_error {
    await pause();
    i32 local = 1;
    i32 seen = add_to(&local, twice(3));
    i32 forwarded = add_to(total, 6);
    i32 read = read_from(shared, twice(1));
    put_first(window, seen);
    i32 number = 0;
    make_number(out number, 4);
    return seen + forwarded + read + number;
}

async i32 main() {
    arc Counter counter = new arc Counter {.drops = 0u32};
    i32 total = 1;
    await pause();
    // A nested call beside an exclusive borrow, an index and a receiver.
    if (add_to(&total, twice(3)) != 7) { return 1; }
    i32[3] items = {1, 2, 3};
    if (add_to(&items[twice(1) - 1], 6) != 8) { return 2; }
    Box box = {.value = 1};
    if (box.add(twice(3)) != 7) { return 3; }
    // Out destinations: a failed call publishes nothing, a successful one replaces once.
    Tracked slot = {.counter = std.arc::clone(&counter), .value = 1};
    try {
        make(out slot, std.arc::clone(&counter), -5);
        i32 unexpected = slot.value;
        return unexpected + 100;
    } catch (Failed failure) {
        if (failure.code != -5) { return 4; }
    }
    if (slot.value != 1) { return 5; }
    await pause();
    try {
        make(out slot, std.arc::clone(&counter), 7);
    } catch (Failed failure) {
        failure as void;
        return 6;
    }
    if (slot.value != 7) { return 7; }
    if (core::atomic_load(&counter->drops, core::memory_order::relaxed) != 1u32) { return 8; }
    // Scoped frames keep their borrows while they call synchronous helpers.
    i32 shared = 10;
    i32[2] window = {0, 0};
    try {
        task_scope(1) group {
            i32 result = await worker(&total, &shared, window[0usize..2usize]);
            if (result != 7 + 13 + 12 + 4) { return 9; }
        }
    } catch (std.async::start_error failure) {
        failure as void;
        return 10;
    }
    if (total != 13 || window[0] != 7) { return 11; }
    return 0;
}
