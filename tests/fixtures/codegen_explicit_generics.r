module test.codegen.explicit_generics;

error Missing { i32 code; };

@generic<T: copy>
array<T> make() throws std.alloc::alloc_error {
    array<T> values = std.array::with_capacity::<T>(4usize);
    return move values;
}

@generic<T: copy, const usize N>
usize width(T value) {
    value as void;
    return N;
}

@generic<T: copy, E: errors>
T relay(T value) throws E { return value; }

@generic<E: errors>
i32 forward(i32 value) throws E {
    i32 result = relay::<i32, throws(E)>(value);
    return result;
}

@generic<E: errors>
i32 attempt(bool fail) throws E, Missing {
    throw (fail == true) Missing { .code = 7 };
    return 1;
}

@generic<T: copy>
T pick(T x) { return x; }

@generic<T: copy, U: copy>
T pick(T x, U y) {
    y as void;
    return x;
}

struct Meter { i32 value; };
i32 Meter::read(const Meter* this) { return this->value; }
Meter Meter::zero() { return Meter { .value = 0 }; }

@generic<T: copy>
struct Cell { T value; };

@generic<T: copy>
T Cell<T>::get(const Cell<T>* this) { return this->value; }

@generic<T: copy, U: copy>
U Cell<T>::convert(const Cell<T>* this, U fallback) {
    this->value as void;
    return fallback;
}

i32 main() {
    array<u16> values = make::<u16>();
    if (len(values) != 0usize) { return 1; }
    auto factory = make::<i64>;
    array<i64> more = factory();
    if (len(more) != 0usize) { return 2; }
    usize w = width::<u8, 8usize>(1u8);
    if (w != 8usize) { return 3; }
    auto wide = width::<bool, 16usize>;
    if (wide(true) != 16usize) { return 4; }
    i32 same = relay::<i32, throws()>(5);
    if (same != 5) { return 5; }
    try {
        i32 forwarded = forward::<throws(Missing)>(6);
        if (forwarded != 6) { return 6; }
    } catch (Missing failure) { return 13; }
    i32 one = pick::<i32>(7);
    if (one != 7) { return 7; }
    i32 two = pick::<i32, bool>(8, true);
    if (two != 8) { return 14; }
    Meter meter = {.value = 9};
    auto read = Meter::read;
    if (read(&meter) != 9) { return 8; }
    auto zero = Meter::zero;
    Meter empty = zero();
    if (read(&empty) != 0) { return 15; }
    Cell<i32> cell = {.value = 10};
    auto get = Cell<i32>::get;
    if (get(&cell) != 10) { return 9; }
    auto convert = Cell<i32>::convert::<u8>;
    if (convert(&cell, 11u8) != 11u8) { return 16; }
    if (cell.convert::<u16>(12u16) != 12u16) { return 10; }
    try {
        i32 failed = attempt::<throws()>(true);
        failed as void;
        return 11;
    } catch (Missing failure) {
        if (failure.code != 7) { return 12; }
    }
    return 0;
}
