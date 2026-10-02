module test.codegen.view_holder_moves;

/* R-STMT-0010, R-BORROW-0018: a Move `switch` moves a payload that holds views out of a variant,
   such as a struct with an exclusive borrow or a container iterator, with the payload's origins;
   the scoped threads of R-MEM-0015 take such values by move. */

struct Slot {
    i32* target;
};

enum Pending {
    Write(Slot),
    Skip,
};

void poke(Slot slot) {
    *slot.target += 5;
}

i32 drain(std.list::iter<i32> cursor) {
    i32 sum = 0;
    while (true) {
        o<const i32*> item = std.list::next(&cursor);
        switch (item) {
            case variant o::some(value):
                sum += **value;
                break;
            case variant o::none:
                return sum;
        }
    }
}

i32 payloads() throws std.list::push_error<i32>, std.dict::insert_error<i32, i32> {
    i32 value = 1;
    o<Slot> slot = o::some(Slot {.target = &value});
    switch (move slot) {
        case variant o::some(move taken):
            *taken.target = 7;
            break;
        case variant o::none:
            return 1;
    }
    if (value != 7) {
        return 2;
    }
    Pending pending = Pending::Write(Slot {.target = &value});
    switch (move pending) {
        case variant Pending::Write(move taken):
            *taken.target += 1;
            break;
        case variant Pending::Skip:
            return 3;
    }
    if (value != 8) {
        return 4;
    }
    list<i32> numbers = std.list::create::<i32>();
    std.list::push_back(&numbers, 2) as void;
    std.list::push_back(&numbers, 3) as void;
    o<std.list::iter<i32>> cursor = o::some(std.list::iter(&numbers));
    switch (move cursor) {
        case variant o::some(move inner):
            if (drain(move inner) != 5) {
                return 5;
            }
            break;
        case variant o::none:
            return 6;
    }
    dict<i32, i32> table = std.dict::create::<i32, i32>();
    std.dict::insert(&table, 4, 40) as void;
    o<std.dict::iter<i32, i32>> pairs = o::some(std.dict::iter(&table));
    switch (move pairs) {
        case variant o::some(move inner):
            o<std.dict::entry_ref<i32, i32>> entry = std.dict::next(&inner);
            switch (entry) {
                case variant o::some(found):
                    if (*(*found).value != 40) {
                        return 7;
                    }
                    break;
                case variant o::none:
                    return 8;
            }
            break;
        case variant o::none:
            return 9;
    }
    return 0;
}

i32 threads() throws std.thread::thread_error, std.list::push_error<i32> {
    i32 value = 1;
    list<i32> numbers = std.list::create::<i32>();
    std.list::push_back(&numbers, 3) as void;
    std.list::push_back(&numbers, 4) as void;
    i32 total = 0;
    thread_scope {
        Slot slot = Slot {.target = &value};
        std.thread::scoped_join_handle<void> first = std.thread::spawn_scoped(poke, move slot);
        std.list::iter<i32> cursor = std.list::iter(&numbers);
        std.thread::scoped_join_handle<i32> second = std.thread::spawn_scoped(drain, move cursor);
        std.thread::join_result<void> joined = std.thread::join(move first);
        drop joined;
        std.thread::join_result<i32> counted = std.thread::join(move second);
        switch (move counted) {
            case variant std.thread::join_result::returned(move count):
                total = count;
                break;
            case variant std.thread::join_result::panicked(move report):
                move report as void;
                return 20;
        }
    }
    if (value != 6) {
        return 21;
    }
    return total - 7;
}

i32 main() {
    try {
        const i32 moved = payloads();
        if (moved != 0) {
            return moved;
        }
        return threads();
    } catch (std.list::push_error<i32> failure) {
        move failure as void;
        return 90;
    } catch (std.dict::insert_error<i32, i32> failure) {
        move failure as void;
        return 91;
    } catch (std.thread::thread_error failure) {
        failure as void;
        return 92;
    }
}
