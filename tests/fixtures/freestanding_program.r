module test.freestanding.program;

/* R-CONF-0005: a freestanding program uses the language and core only. Its entries from C are
   @callback functions and the generated r_freestanding_main; the environment supplies the panic
   handler and the stack bounds. */

thread_local i32 dropped_counters = 0;
thread_local i32 thread_entries = 0;

struct Counter {
    i32 value;
};

drop(Counter* self) {
    dropped_counters += 1;
}

const Counter module_counter = { .value = 7 };

/* R-REFL-0001 and R-REFL-0004 serve freestanding programs without any runtime symbol. */
enum Signal { start, stop, reset, };

protected u32 checksum(const u8[] bytes) {
    u32 total = 0;
    usize index = 0;
    while (index < len(bytes)) {
        total = core::wrapping_mul_u32(total, 31u32);
        total = core::wrapping_add_u32(total, bytes[index] as u32);
        index += 1;
    }
    return total;
}

/* R-FUNC-0026 (L35): bounded recursion needs no runtime symbol either. */
@recursion(depth = 4)
protected u32 digits(u32 value) throws core::recursion_error {
    if (value < 10u32) { return 1u32; }
    u32 rest = digits(value / 10u32);
    return rest + 1u32;
}

protected i32 bounded_digits() {
    try {
        u32 four = digits(1234u32);
        if (four != 4u32) { return 1; }
    } catch (core::recursion_error refused) {
        return 2;
    }
    try {
        u32 five = digits(12345u32);
        five as void;
        return 3;
    } catch (core::recursion_error refused) {
        if (refused.depth != 4usize) { return 4; }
    }
    return 0;
}

@callback
@safety("FREESTANDING-TWICE", "The environment adopted the stack bounds of the calling thread")
extern "C" c_int twice(c_int value) {
    thread_entries += 1;
    return value * 2i32 as c_int;
}

@callback
@safety("FREESTANDING-SELECT", "The environment adopted the stack bounds of the calling thread")
extern "C" c_int select_byte(c_size index) {
    u8[4] bytes = {10u8, 20u8, 30u8, 40u8};
    const u8[] view = &bytes;
    return view[index as usize] as i32 as c_int;
}

@callback
@safety("FREESTANDING-ENTRIES", "The environment adopted the stack bounds of the calling thread")
extern "C" c_int entries() {
    return thread_entries as c_int;
}

@callback
@safety("FREESTANDING-DROPPED", "The environment adopted the stack bounds of the calling thread")
extern "C" c_int dropped() {
    return dropped_counters as c_int;
}

i32 main() {
    u8[5] sample = {1u8, 2u8, 3u8, 4u8, 5u8};
    const u8[] view = &sample;
    const u8[] tail = view[3..];
    if (len(tail) != 2) {
        return 1;
    }
    if (module_counter.value != 7) {
        return 2;
    }
    u32 sum = checksum(view);
    if (sum != 986115) {
        return 3;
    }
    Signal signal = Signal::reset;
    constexpr str signal_name = core::enum_name(signal);
    constexpr str profile = core::profile_name();
    constexpr str spelled = core::type_name::<Signal[3]>();
    Signal[3] signals = core::enum_variants::<Signal>();
    signal_name as void;
    profile as void;
    spelled as void;
    if (signals[0] != core::enum_min::<Signal>() || signals[2] != core::enum_max::<Signal>()) {
        return 4;
    }
    o<Signal> found = core::enum_at::<Signal>(1);
    switch (found) {
    case variant o::some(value):
        if (*value != Signal::stop) {
            return 5;
        }
        break;
    case variant o::none:
        return 6;
    }
    if (core::enum_count::<Signal>() != 3 || core::enum_ordinal(signal) != 2) {
        return 7;
    }
    i32 bounded = bounded_digits();
    if (bounded != 0) {
        return 10 + bounded;
    }
    return 0;
}
