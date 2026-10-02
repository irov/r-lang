module test.codegen.generic_owners;

/* R-TYPE-0009, R-OBJ-0006, R-TYPE-0032: an owner, weak owner or raw pointer names a generic
   instance with its own type arguments, and `new` allocates one directly. */

@generic<T>
struct Cell {
    T value;
    i32 tag;
};

@generic<T>
enum Choice {
    Some(T),
    Nothing,
};

i64 read_raw(raw const Cell<i64>* cell) {
    unsafe {
        return cell->value;
    }
}

i32 unwrap(own Choice<i32>* choice) {
    switch (*choice) {
        case variant Choice<i32>::Some(value):
            return *value;
        case variant Choice<i32>::Nothing:
            return -1;
    }
}

i32 main() {
    own Cell<i32>* boxed = new Cell<i32> {.value = 5, .tag = 1};
    if ((boxed->value != 5) || (boxed->tag != 1)) {
        return 1;
    }
    own Cell<i64>*? maybe = new Cell<i64> {.value = 7i64, .tag = 2};
    if (maybe == null) {
        return 2;
    }
    arc Cell<i32> shared = new arc Cell<i32> {.value = 9, .tag = 3};
    arc Cell<i32> again = std.arc::clone(&shared);
    if ((again->value != 9) || (std.arc::strong_count(&shared) != 2usize)) {
        return 3;
    }
    weak arc Cell<i32> observer = std.arc::downgrade(&shared);
    o<arc Cell<i32>> upgraded = std.arc::upgrade(&observer);
    switch (move upgraded) {
        case variant o::some(move strong):
            if (strong->tag != 3) {
                return 4;
            }
            break;
        case variant o::none:
            return 5;
    }
    Cell<i32> plain = Cell<i32> {.value = 11, .tag = 4};
    rc Cell<i32> counted = new rc Cell<i32>(move plain);
    if (counted->value != 11) {
        return 6;
    }
    Cell<i64> local = Cell<i64> {.value = 13i64, .tag = 5};
    unsafe {
        if (read_raw(&local as raw const Cell<i64>*) != 13i64) {
            return 7;
        }
    }
    if (unwrap(new Choice<i32>(Choice<i32>::Some(17))) != 17) {
        return 8;
    }
    if (unwrap(new Choice<i32>(Choice<i32>::Nothing)) != -1) {
        return 9;
    }
    return 0;
}
