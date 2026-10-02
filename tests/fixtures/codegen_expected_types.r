module test.codegen.expected_types;

import std.sync;

/* R-TYPE-0036 (L17.1): parameters that the arguments of a generic call leave open take the
   expected type of its result. */
@generic<T>
o<T> nothing() { return o::none; }

@generic<T: copy>
o<T> some_default(T value) { return o::some(value); }

@generic<T: copy, U: copy>
U convert_or(T value, U fallback) { value as void; return fallback; }

@generic<T: copy>
array<T> repeated(T value, usize count) throws std.array::push_error<T> {
    array<T> items = std.array::create();
    for (usize index in 0usize..count) {
        std.array::push(&items, value);
    }
    return move items;
}

@generic<T>
struct Slot { o<T> value; };

@generic<T>
o<T> Slot<T>::empty(const Slot<T>* this) { return o::none; }

struct Holder { o<i64> slot; };

o<u8> give() { return nothing(); }

i32 count_some(o<i16> value) {
    switch (move value) {
    case variant o::some(move v): return v as i32;
    case variant o::none: return 0;
    }
}

/* R-TYPE-0036 (L17.2): standard constructors take their type operands from the expected type
   of their result. */
struct Registry { array<i32> values; dict<str, i64> names; list<u8> bytes; };

Registry make_registry() {
    return Registry {.values = std.array::create(), .names = std.dict::create(),
                     .bytes = std.list::create()};
}

i32 main() {
    try {
        o<i32> first = nothing();
        o<i32> second = some_default(7);
        (o<i32>)[2] pair = {nothing(), nothing()};
        Holder holder = Holder {.slot = nothing()};
        Slot<u16> slot = Slot<u16> {.value = o::none};
        o<u16> from_method = slot.empty();
        i64 converted = convert_or(1u8, 5i64);
        array<i32> fives = repeated(5, 3usize);
        array<u16> reserved = std.array::with_capacity(4usize);
        dict<u32, bool> seen = std.dict::with_capacity(8usize);
        std.sync::channel<i32> channel = std.sync::channel();
        std.sync::once_lock<u32> lock = std.sync::once_lock();
        Registry registry = make_registry();
        i32 status = 0;
        switch (move first) {
        case variant o::some(move v): status = 1; v as void; break;
        case variant o::none: break;
        }
        switch (move second) {
        case variant o::some(move v): if (v != 7) { status = 2; } break;
        case variant o::none: status = 2; break;
        }
        pair as void;
        holder as void;
        from_method as void;
        if (converted != 5i64) { status = 3; }
        if (len(fives) != 3usize) { status = 4; }
        if (count_some(nothing()) != 0) { status = 5; }
        o<u8> given = give();
        given as void;
        std.array::push(&registry.values, 4);
        if (len(registry.values) != 1usize) { status = 6; }
        move fives as void;
        move reserved as void;
        move seen as void;
        move channel as void;
        move lock as void;
        move registry as void;
        return status;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 90;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 91;
    }
}
