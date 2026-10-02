module test.codegen.static_values;

/* R-META-0002, R-META-0003: `@if` conditions over constant values, in bodies and at module
   scope. Inactive branches below name missing declarations: they are never checked. */

struct Header { u32 magic; u16 length; u8 flags; };
enum Permission : u8 { read, write, execute, };

usize checked_capacity(usize requested) {
    if (requested == 0usize || requested > 65536usize) {
        panic("capacity out of range");
    }
    return requested;
}

bool is_power_of_two(usize value) {
    return value != 0usize && (value & (value - 1usize)) == 0usize;
}

const usize QUEUE_CAPACITY = checked_capacity(1024usize);
const bool TRACE = false;

/* Module conditions select declarations before any declaration is checked. */
@if (QUEUE_CAPACITY <= 4096usize) {
    struct Queue { u32[QUEUE_CAPACITY] slots; usize head; };
    const usize QUEUE_KIND = 1usize;
} @else {
    struct Queue { Unknown slots; };
    const usize QUEUE_KIND = missing_kind;
}

@if (sizeof(Header) == 8usize) {
    u64 pack(Header header) {
        return (header.magic as u64) | ((header.length as u64) << 32u64);
    }
} @else {
    u8[16] pack(Header header) { return missing_pack; }
}

@if (core::enum_count::<Permission>() <= 8usize) {
    struct PermissionSet { u8 bits; };
} @else {
    struct PermissionSet { u64 bits; };
}

@if (is_power_of_two(QUEUE_CAPACITY) == true) {
    usize wrap(usize index) { return index & (QUEUE_CAPACITY - 1usize); }
} @else {
    usize wrap(usize index) { return index % QUEUE_CAPACITY; }
}

/* A condition may use a declaration that another condition selected. */
@if (QUEUE_KIND == 1usize) {
    @if (false || QUEUE_KIND in 1usize..2usize) {
        const i32 NESTED = 7;
    } @else {
        const i32 NESTED = missing_nested;
    }
}

@if (false) {
    i32 disabled() { return undefined_name; }
} @else @if (true && sizeof(Header) == 4usize) {
    const i32 CHAIN = 1;
} @else @if (!(QUEUE_CAPACITY < 16usize)) {
    const i32 CHAIN = 2;
} @else {
    const i32 CHAIN = 3;
}

u32 Header::total(const Header* this) {
    @if (sizeof(Header) == 8usize) {
        return this->magic + (this->length as u32);
    } @else {
        return missing_total;
    }
}

i32 layout() {
    @if (TRACE == true) {
        return undefined_trace;
    }
    @if (sizeof(Header) == 8usize && QUEUE_CAPACITY in 1usize..4097usize) {
        return 8;
    } @else {
        return missing_layout;
    }
}

/* Dependent conditions are decided for each instantiation. */
@generic<const usize N>
usize strategy((u8[N])* buffer) {
    @if (N <= 64usize) {
        for (usize i = 0usize; i < N; i++) {
            (*buffer)[i] = 0u8;
        }
        return 1usize;
    } @else @if (is_power_of_two(N) == true) {
        return 2usize;
    } @else {
        return 3usize;
    }
}

@generic<T: copy>
usize width(T value) {
    value as void;
    @if (sizeof(T) <= 4usize) {
        return 4usize;
    } @else {
        return 8usize;
    }
}

@generic<const usize N>
usize half() {
    const usize HALF = N / 2usize;
    @if (HALF in 1usize..8usize) {
        return HALF;
    } @else {
        return 0usize;
    }
}

/* A generic call closes with the arguments of each instantiation; BYTES is read only here. */
@generic<T>
usize size_of() {
    return sizeof(T);
}

@generic<const usize N>
usize doubled() {
    return N * 2usize;
}

@generic<T: copy, const usize N>
usize fits(T value) {
    value as void;
    const usize BYTES = size_of::<T>() * N;
    @if (BYTES <= 16usize && doubled::<N>() in 1usize..9usize) {
        return 1usize;
    } @else {
        return 2usize;
    }
}

@generic<T, const usize N>
usize classify(T value) {
    @if (T is copy && N <= 4usize) {
        T copied = value;
        copied as void;
        return 1usize;
    } @else @if (!(T is copy) || N > 100usize) {
        return 2usize;
    } @else {
        return 3usize;
    }
}

i32 main() {
    Queue queue = {};
    PermissionSet set = {};
    Header header = {.magic = 1u32, .length = 2u16, .flags = 0u8};
    u8[16] small = {};
    u8[128] power = {};
    u8[100] odd = {};
    bool module_ok = len(queue.slots) == 1024usize && QUEUE_KIND == 1usize &&
                     sizeof(PermissionSet) == 1usize && set.bits == 0u8 && NESTED == 7 &&
                     CHAIN == 2;
    bool body_ok = pack(header) == 0x200000001u64 && wrap(1030usize) == 6usize &&
                   header.total() == 3u32 && layout() == 8;
    bool generic_ok = strategy(&small) == 1usize && strategy(&power) == 2usize &&
                      strategy(&odd) == 3usize && width(1u8) == 4usize &&
                      width(1u64) == 8usize && half::<6usize>() == 3usize &&
                      half::<40usize>() == 0usize;
    bool mixed_ok = classify::<i32, 3usize>(1) == 1usize &&
                    classify::<i32, 200usize>(1) == 2usize &&
                    classify::<i32, 50usize>(1) == 3usize;
    bool call_ok = fits::<u32, 4usize>(1u32) == 1usize && fits::<u64, 4usize>(1u64) == 2usize &&
                   fits::<u8, 8usize>(1u8) == 2usize;
    if (module_ok == false || body_ok == false || generic_ok == false || mixed_ok == false ||
        call_ok == false) {
        return 1;
    }
    return 0;
}
