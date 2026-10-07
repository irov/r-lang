module std.cbor;

/* R-SLIB-CBOR-0001: why bytes are not a CBOR data item of RFC 8949, or not its deterministic
   encoding. */
@derive(format)
enum error_code {
    truncated,
    malformed,
    invalid_utf8,
    duplicate_key,
    too_deep,
    trailing_data,
    not_deterministic,
};

/* R-SLIB-CBOR-0001: a failure with the offset of the item in the input where it was found. */
error cbor_error { error_code code; usize offset; };

protected cbor_error failure(error_code code, usize offset) {
    return cbor_error {.code = code, .offset = offset};
}

/* R-SLIB-CBOR-0002: one CBOR data item. A negative integer holds n for the value -1 - n, so
   every integer of major types 0 and 1 is represented; a tag holds its content behind an
   owner. */
enum value {
    unsigned(u64),
    negative(u64),
    bytes(bytes),
    text(std.string::string),
    sequence(array<value>),
    map(array<entry>),
    tagged { u64 tag; own value* content; },
    simple(u8),
    boolean(bool),
    null_value,
    undefined,
    float(f64),
};

/* R-SLIB-CBOR-0002: one key and its value in a map. */
struct entry { value key; value item; };

/* The deepest nesting of arrays, maps and tags that decode accepts. */
protected const usize MAX_DEPTH = 1024usize;

/* ---- Constructors ---- */

value value::integer(i64 number) {
    if (number >= 0i64) { return value::unsigned(number as u64); }
    return value::negative(((-1i64) - number) as u64);
}

value value::of_bytes(const u8[] data) throws std.alloc::alloc_error {
    bytes copied = std.bytes::with_capacity(len(data));
    std.bytes::append(&copied, data);
    return value::bytes(move copied);
}

value value::of_text(str data) throws std.alloc::alloc_error {
    std.string::string copied = std.string::from_str(data);
    return value::text(move copied);
}

value value::tag_of(u64 tag, value content) throws std.alloc::alloc_error {
    own value* boxed = new value(move content);
    return value::tagged {.tag = tag, .content = move boxed};
}

/* ---- Heads ---- */

protected void big_endian(bytes* out, u64 number, usize count) throws std.alloc::alloc_error {
    for (usize index = count; index > 0usize; index -= 1usize) {
        u64 shift = ((index - 1usize) * 8usize) as u64;
        std.bytes::append_u8(out, ((number >> shift) & 0xffu64) as u8);
    }
}

/* The head of an item of major type `major` with argument `argument`, in its shortest form
   (RFC 8949 section 4.2.1). */
protected void head(bytes* out, u8 major, u64 argument) throws std.alloc::alloc_error {
    const u8 initial = (major * 32u8) as u8;
    if (argument < 24u64) {
        std.bytes::append_u8(out, (initial + (argument as u8)) as u8);
        return;
    }
    if (argument <= 0xffu64) {
        std.bytes::append_u8(out, (initial + 24u8) as u8);
        std.bytes::append_u8(out, argument as u8);
        return;
    }
    if (argument <= 0xffffu64) {
        std.bytes::append_u8(out, (initial + 25u8) as u8);
        big_endian(out, argument, 2usize);
        return;
    }
    if (argument <= 0xffffffffu64) {
        std.bytes::append_u8(out, (initial + 26u8) as u8);
        big_endian(out, argument, 4usize);
        return;
    }
    std.bytes::append_u8(out, (initial + 27u8) as u8);
    big_endian(out, argument, 8usize);
}


/* ---- Floats ---- */

/* number * 2^exponent; the callers stay within the finite range, where it is exact. */
protected f64 scale(f64 number, i32 exponent) {
    try {
        return std.math::compose_binary_f64(number, exponent);
    } catch (std.math::math_error ignored) {
        ignored as void;
    }
    return number;
}

/* The bits of a binary16, binary32 or binary64 encoding of `number`, computed from its exact
   binary parts; `width` is 16, 32 or 64. NaN takes the quiet NaN with no payload. */
protected u64 float_bits(f64 number, u32 width) {
    u64 exponent_bits = 5u64;
    u64 fraction_bits = 10u64;
    if (width == 32u32) {
        exponent_bits = 8u64;
        fraction_bits = 23u64;
    }
    if (width == 64u32) {
        exponent_bits = 11u64;
        fraction_bits = 52u64;
    }
    const u64 bias = (1u64 << (exponent_bits - 1u64)) - 1u64;
    const u64 all_ones = (1u64 << exponent_bits) - 1u64;
    u64 sign = 0u64;
    if (std.math::sign_bit_f64(number) == true) { sign = 1u64 << (exponent_bits + fraction_bits); }
    if (std.math::is_nan_f64(number) == true) {
        return (all_ones << fraction_bits) | (1u64 << (fraction_bits - 1u64));
    }
    if (std.math::is_infinite_f64(number) == true) { return sign | (all_ones << fraction_bits); }
    if (number == 0.0) { return sign; }
    const f64 magnitude = std.math::abs_f64(number);
    std.math::binary_parts_f64 parts = std.math::split_binary_f64(magnitude);
    // magnitude = fraction * 2^exponent with 0.5 <= fraction < 1, so the unbiased exponent of
    // the leading bit is exponent - 1.
    const i64 unbiased = (parts.exponent as i64) - 1i64;
    const i64 least = 1i64 - (bias as i64);
    if (unbiased >= least) {
        // Normal: the fraction field is (2 * fraction - 1) * 2^fraction_bits.
        const f64 scaled = scale(parts.fraction, (fraction_bits as i32) + 1i32);
        const u64 mantissa = (scaled as u64) - (1u64 << fraction_bits);
        const u64 biased = ((unbiased + (bias as i64)) as u64);
        return sign | (biased << fraction_bits) | mantissa;
    }
    // Subnormal: the fraction field is magnitude * 2^(bias - 1 + fraction_bits).
    const f64 scaled = scale(magnitude, ((bias as i32) - 1i32) + (fraction_bits as i32));
    return sign | (scaled as u64);
}

/* The value of the binary16, binary32 or binary64 bits `bits`. */
protected f64 float_from_bits(u64 bits, u32 width) {
    u64 exponent_bits = 5u64;
    u64 fraction_bits = 10u64;
    if (width == 32u32) {
        exponent_bits = 8u64;
        fraction_bits = 23u64;
    }
    if (width == 64u32) {
        exponent_bits = 11u64;
        fraction_bits = 52u64;
    }
    const u64 bias = (1u64 << (exponent_bits - 1u64)) - 1u64;
    const u64 all_ones = (1u64 << exponent_bits) - 1u64;
    const bool negative = ((bits >> (exponent_bits + fraction_bits)) & 1u64) == 1u64;
    const u64 biased = (bits >> fraction_bits) & all_ones;
    const u64 mantissa = bits & ((1u64 << fraction_bits) - 1u64);
    f64 magnitude = 0.0;
    if (biased == all_ones) {
        if (mantissa != 0u64) { return 0.0 / 0.0; }
        magnitude = 1.0 / 0.0;
    } else {
        i32 exponent = 1i32 - (bias as i32) - (fraction_bits as i32);
        u64 significand = mantissa;
        if (biased != 0u64) {
            exponent = (biased as i32) - (bias as i32) - (fraction_bits as i32);
            significand = mantissa | (1u64 << fraction_bits);
        }
        magnitude = scale(significand as f64, exponent);
    }
    if (negative == true) { return std.math::copy_sign_f64(magnitude, -1.0); }
    return magnitude;
}

/* Whether the binary16 encoding of `number` is exact: its binary parts fit the 11-bit
   significand and the exponent range of binary16, subnormals included. */
protected bool fits_half(f64 number) {
    if (std.math::is_infinite_f64(number) == true || number == 0.0) { return true; }
    const f64 magnitude = std.math::abs_f64(number);
    std.math::binary_parts_f64 parts = std.math::split_binary_f64(magnitude);
    const i32 unbiased = parts.exponent - 1i32;
    if (unbiased > 15i32 || unbiased < -24i32) { return false; }
    // The significand scaled to keep its 11 bits, or fewer for a subnormal, shall be an integer.
    i32 kept = 11i32;
    if (unbiased < -14i32) { kept = 11i32 - (-14i32 - unbiased); }
    const f64 scaled = scale(parts.fraction, kept);
    return std.math::trunc_f64(scaled) == scaled;
}

/* The shortest of binary16, binary32 and binary64 that holds `number` exactly (RFC 8949
   section 4.2.2): 16, 32 or 64. Every NaN takes binary16. */
protected u32 float_width(f64 number) {
    if (std.math::is_nan_f64(number) == true) { return 16u32; }
    if (float_from_bits(float_bits(number, 16u32), 16u32) == number &&
        fits_half(number) == true) {
        return 16u32;
    }
    if (((number as f32) as f64) == number) { return 32u32; }
    return 64u32;
}


protected void append_float(bytes* out, f64 number) throws std.alloc::alloc_error {
    const u32 width = float_width(number);
    const u64 bits = float_bits(number, width);
    if (width == 16u32) {
        std.bytes::append_u8(out, 0xf9u8);
        big_endian(out, bits, 2usize);
        return;
    }
    if (width == 32u32) {
        std.bytes::append_u8(out, 0xfau8);
        big_endian(out, bits, 4usize);
        return;
    }
    std.bytes::append_u8(out, 0xfbu8);
    big_endian(out, bits, 8usize);
}

protected bool key_before(const u8[] left, const u8[] right) {
    usize shared = len(left);
    if (len(right) < shared) { shared = len(right); }
    for (usize index = 0usize; index < shared; index += 1usize) {
        if (left[index] != right[index]) { return left[index] < right[index]; }
    }
    return len(left) < len(right);
}

protected bool same_bytes(const u8[] left, const u8[] right) {
    if (len(left) != len(right)) { return false; }
    for (usize index = 0usize; index < len(left); index += 1usize) {
        if (left[index] != right[index]) { return false; }
    }
    return true;
}

protected void push_key(array<bytes>* keys, bytes key) throws std.alloc::alloc_error {
    try {
        keys->push(move key);
    } catch (std.array::push_error<bytes> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* The order of `keys` by their bytes (insertion sort over indices); two equal keys are a
   duplicate key. */
protected array<usize> sorted_order(const array<bytes>* keys, usize offset)
    throws std.alloc::alloc_error, cbor_error {
    array<usize> order = [];
    try {
        for (usize index = 0usize; index < len(*keys); index += 1usize) {
            order.push(index);
        }
    } catch (std.array::push_error<usize> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
    const bytes[] listed = std.array::as_slice(keys);
    for (usize index = 1usize; index < len(order); index += 1usize) {
        usize at = index;
        while (at > 0usize &&
               key_before(listed[order[at]].as_slice(), listed[order[at - 1usize]].as_slice()) == true) {
            const usize moved = order[at];
            order[at] = order[at - 1usize];
            order[at - 1usize] = moved;
            at -= 1usize;
        }
        if (same_bytes(listed[order[at]].as_slice(), listed[order[index]].as_slice()) == true &&
            at != index) {
            throw failure(error_code::duplicate_key, offset);
        }
    }
    for (usize index = 1usize; index < len(order); index += 1usize) {
        if (same_bytes(listed[order[index]].as_slice(), listed[order[index - 1usize]].as_slice()) == true) {
            throw failure(error_code::duplicate_key, offset);
        }
    }
    return move order;
}

/* A simple value: 0 to 23 in the initial byte, 32 to 255 in the next one; 24 to 31 have no
   well-formed encoding (RFC 8949 section 3.3). */
protected void append_simple(bytes* out, u8 number) throws std.alloc::alloc_error, cbor_error {
    if (number < 24u8) {
        std.bytes::append_u8(out, (0xe0u8 + number) as u8);
        return;
    }
    if (number < 32u8) { throw failure(error_code::malformed, len(*out)); }
    std.bytes::append_u8(out, 0xf8u8);
    std.bytes::append_u8(out, number);
}

/* An item that is neither an array, a map nor a tag. */
protected void encode_scalar(bytes* out, const value* item) throws std.alloc::alloc_error, cbor_error {
    switch (*item) {
    case variant value::unsigned(number): head(out, 0u8, *number);
    case variant value::negative(number): head(out, 1u8, *number);
    case variant value::bytes(data):
        head(out, 2u8, len(*data) as u64);
        std.bytes::append(out, std.array::as_slice(data));
    case variant value::text(data):
        const u8[] raw_bytes = *data;
        head(out, 3u8, len(raw_bytes) as u64);
        std.bytes::append(out, raw_bytes);
    case variant value::simple(number): append_simple(out, *number);
    case variant value::boolean(truth):
        if (*truth == true) {
            std.bytes::append_u8(out, 0xf5u8);
        } else {
            std.bytes::append_u8(out, 0xf4u8);
        }
    case variant value::null_value: std.bytes::append_u8(out, 0xf6u8);
    case variant value::undefined: std.bytes::append_u8(out, 0xf7u8);
    case variant value::float(number): append_float(out, *number);
    default: panic("std.cbor: a container reached the scalar encoder");
    }
}

protected u8 take_u8(array<u8>* target) {
    o<u8> popped = std.array::pop(target);
    switch (popped) {
    case variant o::some(number): return *number;
    case variant o::none: break;
    }
    panic("std.cbor: the decoder lost a container");
}

protected u64 take_u64(array<u64>* target) {
    o<u64> popped = std.array::pop(target);
    switch (popped) {
    case variant o::some(number): return *number;
    case variant o::none: break;
    }
    panic("std.cbor: the decoder lost a container");
}

protected usize take_usize(array<usize>* target) {
    o<usize> popped = std.array::pop(target);
    switch (popped) {
    case variant o::some(number): return *number;
    case variant o::none: break;
    }
    panic("std.cbor: the decoder lost a container");
}


/* ---- Deterministic encoding of a value ---- */

/* The deterministic encoder works from explicit stacks, so nesting costs heap memory instead of
   call stack: a task encodes one item into the innermost buffer, starts a buffer for a map key
   or value, files a finished buffer as the key or value of the innermost map, or writes a
   complete map in the order of its encoded keys. */
protected const u8 TASK_VALUE = 0u8;
protected const u8 TASK_BUFFER = 1u8;
protected const u8 TASK_KEY = 2u8;
protected const u8 TASK_ITEM = 3u8;
protected const u8 TASK_MAP = 4u8;


protected void push_empty(array<bytes>* buffers) throws std.alloc::alloc_error {
    bytes empty = {};
    push_key(buffers, move empty);
}

protected void push_list(array<array<bytes>>* lists) throws std.alloc::alloc_error {
    try {
        lists->push([]);
    } catch (std.array::push_error<array<bytes>> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected bytes pop_bytes(array<bytes>* buffers) {
    o<bytes> popped = std.array::pop(buffers);
    switch (move popped) {
    case variant o::some(move buffer): return move buffer;
    case variant o::none: break;
    }
    panic("std.cbor: the encoder lost a buffer");
}

protected array<bytes> pop_list(array<array<bytes>>* lists) {
    o<array<bytes>> popped = std.array::pop(lists);
    switch (move popped) {
    case variant o::some(move found): return move found;
    case variant o::none: break;
    }
    panic("std.cbor: the encoder lost a map");
}


/* A complete map: its keys and values in the order of the encoded keys. */
protected void finish_map(array<bytes>* buffers, array<array<bytes>>* keys, array<array<bytes>>* items)
    throws std.alloc::alloc_error, cbor_error {
    array<bytes> map_keys = pop_list(keys);
    array<bytes> map_items = pop_list(items);
    bytes* out = &(*buffers)[len(*buffers) - 1usize];
    if (len(map_items) != len(map_keys)) { panic("std.cbor: a map lost a value"); }
    array<usize> order = sorted_order(&map_keys, len(*out));
    for (usize index = 0usize; index < len(order); index += 1usize) {
        std.bytes::append(out, map_keys[order[index]].as_slice());
        std.bytes::append(out, map_items[order[index]].as_slice());
    }
}


protected void push_job(array<u8>* kinds, array<usize>* depths, u8 kind, usize depth)
    throws std.alloc::alloc_error {
    try {
        kinds->push(kind);
        depths->push(depth);
    } catch (std.array::push_error<u8> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    } catch (std.array::push_error<usize> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* The encoding of `item` appended to `result`. The stack of items still to encode holds
   borrows into `item`, so it is pushed and popped in this function only. */
protected void encode_tree(bytes* result, const value* item)
    throws std.alloc::alloc_error, cbor_error, std.array::push_error<const value*> {
    array<bytes> buffers = [];
    array<array<bytes>> keys = [];
    array<array<bytes>> items = [];
    array<u8> kinds = [];
    array<usize> depths = [];
    array<const value*> pending = [];
    push_empty(&buffers);
    push_job(&kinds, &depths, TASK_VALUE, 0usize);
    pending.push(item);
    while (len(kinds) > 0usize) {
        const u8 job = take_u8(&kinds);
        const usize depth = take_usize(&depths);
        o<const value*> popped = std.array::pop(&pending);
        if (job != TASK_VALUE) {
            popped as void;
            depth as void;
            if (job == TASK_BUFFER) {
                push_empty(&buffers);
            } else {
                if (job == TASK_MAP) {
                    finish_map(&buffers, &keys, &items);
                } else {
                    bytes finished = pop_bytes(&buffers);
                    if (job == TASK_KEY) {
                        push_key(&keys[len(keys) - 1usize], move finished);
                    } else {
                        push_key(&items[len(items) - 1usize], move finished);
                    }
                }
            }
            continue;
        }
        switch (popped) {
        case variant o::some(found):
            const value* current = *found;
            bytes* out = &buffers[len(buffers) - 1usize];
            if (depth > MAX_DEPTH) { throw failure(error_code::too_deep, len(*out)); }
            switch (*current) {
            case variant value::sequence(elements):
                const value[] listed = std.array::as_slice(elements);
                head(out, 4u8, len(listed) as u64);
                for (usize index = len(listed); index > 0usize; index -= 1usize) {
                    push_job(&kinds, &depths, TASK_VALUE, depth + 1usize);
                    pending.push(&listed[index - 1usize]);
                }
            case variant value::map(entries):
                const entry[] listed = std.array::as_slice(entries);
                head(out, 5u8, len(listed) as u64);
                push_list(&keys);
                push_list(&items);
                push_job(&kinds, &depths, TASK_MAP, depth);
                pending.push(current);
                for (usize index = len(listed); index > 0usize; index -= 1usize) {
                    const entry* pair = &listed[index - 1usize];
                    push_job(&kinds, &depths, TASK_ITEM, depth);
                    pending.push(current);
                    push_job(&kinds, &depths, TASK_VALUE, depth + 1usize);
                    pending.push(&pair->item);
                    push_job(&kinds, &depths, TASK_BUFFER, depth);
                    pending.push(current);
                    push_job(&kinds, &depths, TASK_KEY, depth);
                    pending.push(current);
                    push_job(&kinds, &depths, TASK_VALUE, depth + 1usize);
                    pending.push(&pair->key);
                    push_job(&kinds, &depths, TASK_BUFFER, depth);
                    pending.push(current);
                }
            case variant value::tagged(tagged):
                head(out, 6u8, tagged->tag);
                push_job(&kinds, &depths, TASK_VALUE, depth + 1usize);
                pending.push(&*tagged->content);
            default: encode_scalar(out, current);
            }
        case variant o::none: depth as void;
        }
    }
    if (len(keys) != 0usize || len(items) != 0usize) { panic("std.cbor: a map was left open"); }
    bytes encoded = pop_bytes(&buffers);
    std.bytes::append(result, encoded.as_slice());
}

protected void encode_into(bytes* result, const value* item) throws std.alloc::alloc_error, cbor_error {
    try {
        encode_tree(result, item);
    } catch (std.array::push_error<const value*> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* R-SLIB-CBOR-0004: the core deterministic encoding of RFC 8949 section 4.2.1. */
bytes encode(const value* item) throws std.alloc::alloc_error, cbor_error {
    bytes out = {};
    encode_into(&out, item);
    return move out;
}

/* ---- Streaming encoder ---- */

/* R-SLIB-CBOR-0003: appends data items in preferred serialization. Arrays and maps are written
   as a head with their length followed by their elements, which the caller writes next. */
struct encoder { protected bytes output; };

encoder encoder::create() { return encoder {.output = {}}; }

void encoder::unsigned(encoder* this, u64 number) throws std.alloc::alloc_error {
    head(&this->output, 0u8, number);
}

void encoder::negative(encoder* this, u64 number) throws std.alloc::alloc_error {
    head(&this->output, 1u8, number);
}

void encoder::integer(encoder* this, i64 number) throws std.alloc::alloc_error {
    if (number >= 0i64) {
        head(&this->output, 0u8, number as u64);
        return;
    }
    head(&this->output, 1u8, ((-1i64) - number) as u64);
}

void encoder::bytes(encoder* this, const u8[] data) throws std.alloc::alloc_error {
    head(&this->output, 2u8, len(data) as u64);
    std.bytes::append(&this->output, data);
}

void encoder::text(encoder* this, str data) throws std.alloc::alloc_error {
    const u8[] raw_bytes = data;
    head(&this->output, 3u8, len(raw_bytes) as u64);
    std.bytes::append(&this->output, raw_bytes);
}

void encoder::begin_array(encoder* this, u64 count) throws std.alloc::alloc_error {
    head(&this->output, 4u8, count);
}

void encoder::begin_map(encoder* this, u64 count) throws std.alloc::alloc_error {
    head(&this->output, 5u8, count);
}

void encoder::tag(encoder* this, u64 number) throws std.alloc::alloc_error {
    head(&this->output, 6u8, number);
}

void encoder::simple(encoder* this, u8 number) throws std.alloc::alloc_error, cbor_error {
    append_simple(&this->output, number);
}

void encoder::boolean(encoder* this, bool truth) throws std.alloc::alloc_error {
    if (truth == true) {
        std.bytes::append_u8(&this->output, 0xf5u8);
        return;
    }
    std.bytes::append_u8(&this->output, 0xf4u8);
}

void encoder::null_value(encoder* this) throws std.alloc::alloc_error {
    std.bytes::append_u8(&this->output, 0xf6u8);
}

void encoder::undefined(encoder* this) throws std.alloc::alloc_error {
    std.bytes::append_u8(&this->output, 0xf7u8);
}

void encoder::float(encoder* this, f64 number) throws std.alloc::alloc_error {
    append_float(&this->output, number);
}

/* Appends bytes that are already one encoded data item, such as a nested message. */
void encoder::encoded(encoder* this, const u8[] item) throws std.alloc::alloc_error {
    std.bytes::append(&this->output, item);
}

/* The deterministic encoding of `item` (R-SLIB-CBOR-0004). */
void encoder::value(encoder* this, const value* item) throws std.alloc::alloc_error, cbor_error {
    encode_into(&this->output, item);
}

usize encoder::length(const encoder* this) { return len(this->output); }

bytes encoder::finish(encoder* this) {
    bytes result = {};
    bytes taken = core::replace(&this->output, move result);
    return move taken;
}


/* ---- Decoding ---- */

/* The argument of a head whose additional information is `info`, read at `*at`; in strict
   mode it shall be in its shortest form. */
protected u64 read_argument(const u8[] input, usize* at, u8 info, usize start, bool strict)
    throws cbor_error {
    if (info < 24u8) { return info as u64; }
    usize count = 8usize;
    if (info == 24u8) { count = 1usize; }
    if (info == 25u8) { count = 2usize; }
    if (info == 26u8) { count = 4usize; }
    if (info > 27u8) { throw failure(error_code::malformed, start); }
    if (len(input) - *at < count) { throw failure(error_code::truncated, start); }
    u64 number = 0u64;
    for (usize index = 0usize; index < count; index += 1usize) {
        number = (number << 8u64) | (input[*at + index] as u64);
    }
    *at += count;
    if (strict == true) {
        const bool shortest = (count == 1usize && number >= 24u64) ||
                              (count == 2usize && number > 0xffu64) ||
                              (count == 4usize && number > 0xffffu64) ||
                              (count == 8usize && number > 0xffffffffu64);
        if (shortest == false) { throw failure(error_code::not_deterministic, start); }
    }
    return number;
}

protected usize checked_length(const u8[] input, usize at, u64 length, usize start)
    throws cbor_error {
    if ((len(input) - at) as u64 < length) { throw failure(error_code::truncated, start); }
    return length as usize;
}

/* The bytes of a byte or text string of major type `major`, definite or made of definite
   chunks of the same type up to a break; strict mode rejects the indefinite form. */
protected bytes read_string(const u8[] input, usize* at, u8 major, u8 info, usize start, bool strict)
    throws std.alloc::alloc_error, cbor_error {
    bytes data = {};
    if (info != 31u8) {
        const u64 length = read_argument(input, at, info, start, strict);
        const usize count = checked_length(input, *at, length, start);
        std.bytes::append(&data, input[*at..*at + count]);
        *at += count;
        return move data;
    }
    if (strict == true) { throw failure(error_code::not_deterministic, start); }
    while (true) {
        if (*at >= len(input)) { throw failure(error_code::truncated, start); }
        const usize chunk_start = *at;
        const u8 initial = input[*at];
        if (initial == 0xffu8) {
            *at += 1usize;
            return move data;
        }
        if (((initial >> 5u8) as u8) != major || ((initial & 31u8) as u8) == 31u8) {
            throw failure(error_code::malformed, chunk_start);
        }
        *at += 1usize;
        const u64 length = read_argument(input, at, (initial & 31u8) as u8, chunk_start, false);
        const usize count = checked_length(input, *at, length, chunk_start);
        if (major == 3u8) {
            try {
                str checked = core::validate_utf8(input[*at..*at + count]);
                checked as void;
            } catch (core::utf8_error rejected) {
                rejected as void;
                throw failure(error_code::invalid_utf8, chunk_start);
            }
        }
        std.bytes::append(&data, input[*at..*at + count]);
        *at += count;
    }
    return move data;
}

protected value text_value(bytes data, usize start) throws cbor_error {
    std.string::from_bytes_result checked = std.string::from_bytes(move data);
    switch (move checked) {
    case variant std.string::from_bytes_result::valid(move text): return value::text(move text);
    case variant std.string::from_bytes_result::invalid(move rejected):
        drop rejected;
        throw failure(error_code::invalid_utf8, start);
    }
}

/* A simple value or a float of major type 7 other than the break code. */
protected value read_special(const u8[] input, usize* at, u8 info, usize start, bool strict)
    throws cbor_error {
    if (info < 20u8) { return value::simple(info); }
    if (info == 20u8) { return value::boolean(false); }
    if (info == 21u8) { return value::boolean(true); }
    if (info == 22u8) { return value::null_value; }
    if (info == 23u8) { return value::undefined; }
    if (info == 24u8) {
        if (*at >= len(input)) { throw failure(error_code::truncated, start); }
        const u8 number = input[*at];
        *at += 1usize;
        // Values below 32 have the one-byte form only (RFC 8949 section 3.3).
        if (number < 32u8) { throw failure(error_code::malformed, start); }
        return value::simple(number);
    }
    if (info > 27u8) { throw failure(error_code::malformed, start); }
    u32 width = 64u32;
    if (info == 25u8) { width = 16u32; }
    if (info == 26u8) { width = 32u32; }
    const u64 bits = read_argument(input, at, info, start, false);
    const f64 number = float_from_bits(bits, width);
    if (strict == true) {
        const bool canonical_nan = std.math::is_nan_f64(number) == false || (width == 16u32 && bits == 0x7e00u64);
        if (float_width(number) != width || canonical_nan == false) {
            throw failure(error_code::not_deterministic, start);
        }
    }
    return value::float(number);
}

protected void push_value(array<value>* items, value item) throws std.alloc::alloc_error {
    try {
        items->push(move item);
    } catch (std.array::push_error<value> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected void push_entry(array<entry>* entries, entry pair) throws std.alloc::alloc_error {
    try {
        entries->push(move pair);
    } catch (std.array::push_error<entry> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Whether the next byte at `*at` ends an indefinite-length item; it is consumed then. */
protected bool at_break(const u8[] input, usize* at, usize start) throws cbor_error {
    if (*at >= len(input)) { throw failure(error_code::truncated, start); }
    if (input[*at] != 0xffu8) { return false; }
    *at += 1usize;
    return true;
}

/* The initial byte at `*at`, not consumed. */
protected u8 peek(const u8[] input, usize at) throws cbor_error {
    if (at >= len(input)) { throw failure(error_code::truncated, at); }
    return input[at];
}

/* The number of elements of the array or map whose head is at `*at`, after the head; the
   maximum u64 stands for the indefinite length. */
protected const u64 INDEFINITE = 0xffffffffffffffffu64;

protected u64 container_head(const u8[] input, usize* at, bool strict) throws cbor_error {
    const usize start = *at;
    const u8 info = (peek(input, *at) & 31u8) as u8;
    *at += 1usize;
    if (info == 31u8) {
        if (strict == true) { throw failure(error_code::not_deterministic, start); }
        return INDEFINITE;
    }
    const u64 count = read_argument(input, at, info, start, strict);
    // Every element takes at least one byte, so a longer count is truncated input.
    const usize checked = checked_length(input, *at, count, start);
    checked as void;
    return count;
}

/* The tag number of the tag head at `*at`, after the head. */
protected u64 tag_head(const u8[] input, usize* at, bool strict) throws cbor_error {
    const usize start = *at;
    const u8 info = (peek(input, *at) & 31u8) as u8;
    *at += 1usize;
    if (info == 31u8) { throw failure(error_code::malformed, start); }
    return read_argument(input, at, info, start, strict);
}

/* An item of major type 0, 1, 2, 3 or 7 at `*at`. */
protected value decode_scalar(const u8[] input, usize* at, bool strict)
    throws std.alloc::alloc_error, cbor_error {
    const usize start = *at;
    const u8 initial = peek(input, *at);
    *at += 1usize;
    const u8 major = (initial >> 5u8) as u8;
    const u8 info = (initial & 31u8) as u8;
    if (major == 2u8) { return value::bytes(read_string(input, at, major, info, start, strict)); }
    if (major == 3u8) { return text_value(read_string(input, at, major, info, start, strict), start); }
    if (info == 31u8) { throw failure(error_code::malformed, start); }
    if (major == 7u8) { return read_special(input, at, info, start, strict); }
    const u64 argument = read_argument(input, at, info, start, strict);
    if (major == 0u8) { return value::unsigned(argument); }
    return value::negative(argument);
}

/* The decoder keeps the open arrays, maps and tags on explicit stacks: for each its major
   type, its element count (INDEFINITE for the indefinite length; a map counts entries), its tag
   number, the offset of its head and the items read so far, a map holding keys and values in
   turn. For a map it also keeps the input span of the current and previous key, which strict
   mode compares. */
protected struct decoding {
    protected array<u8> kinds;
    protected array<u64> counts;
    protected array<u64> tags;
    protected array<usize> starts;
    protected array<array<value>> parts;
    protected array<usize> key_starts;
    protected array<usize> previous_starts;
    protected array<usize> previous_ends;
};

protected void push_usize(array<usize>* target, usize number) throws std.alloc::alloc_error {
    try {
        target->push(number);
    } catch (std.array::push_error<usize> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected void push_u64(array<u64>* target, u64 number) throws std.alloc::alloc_error {
    try {
        target->push(number);
    } catch (std.array::push_error<u64> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected void open_frame(decoding* state, u8 kind, u64 count, u64 tag, usize start)
    throws std.alloc::alloc_error, cbor_error {
    if (len(state->kinds) >= MAX_DEPTH) { throw failure(error_code::too_deep, start); }
    try {
        state->kinds.push(kind);
        state->parts.push([]);
    } catch (std.array::push_error<u8> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    } catch (std.array::push_error<array<value>> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
    push_u64(&state->counts, count);
    push_u64(&state->tags, tag);
    push_usize(&state->starts, start);
    push_usize(&state->key_starts, start);
    push_usize(&state->previous_starts, 0usize);
    push_usize(&state->previous_ends, 0usize);
}

/* Whether the innermost container is complete; the break of an indefinite one is consumed. */
protected bool frame_complete(decoding* state, const u8[] input, usize* at) throws cbor_error {
    const usize top = len(state->kinds) - 1usize;
    const u64 filled = len(state->parts[top]) as u64;
    if (state->counts[top] == INDEFINITE) {
        // A break between a key and its value is malformed; the item reader reports it.
        if (state->kinds[top] == 5u8 && filled % 2u64 == 1u64) { return false; }
        return at_break(input, at, state->starts[top]);
    }
    if (state->kinds[top] == 5u8) { return filled == state->counts[top] * 2u64; }
    return filled == state->counts[top];
}




protected value take_value(array<value>* target) {
    o<value> popped = std.array::pop(target);
    switch (move popped) {
    case variant o::some(move item): return move item;
    case variant o::none: break;
    }
    panic("std.cbor: the decoder lost an item");
}

protected array<value> take_parts(array<array<value>>* target) {
    o<array<value>> popped = std.array::pop(target);
    switch (move popped) {
    case variant o::some(move items): return move items;
    case variant o::none: break;
    }
    panic("std.cbor: the decoder lost a container");
}

/* The entries of a map from its keys and values in turn; without strict ordering, keys whose
   deterministic encodings are equal are duplicates. */
protected value map_value(array<value> parts, usize start, bool strict)
    throws std.alloc::alloc_error, cbor_error {
    array<value> pairs = move parts;
    array<entry> reversed = [];
    while (len(pairs) > 0usize) {
        value item = take_value(&pairs);
        value key = take_value(&pairs);
        push_entry(&reversed, entry {.key = move key, .item = move item});
    }
    array<entry> entries = [];
    array<bytes> keys = [];
    while (len(reversed) > 0usize) {
        o<entry> popped = std.array::pop(&reversed);
        switch (move popped) {
        case variant o::some(move pair):
            if (strict == false) {
                bytes encoded = {};
                encode_into(&encoded, &pair.key);
                push_key(&keys, move encoded);
            }
            push_entry(&entries, move pair);
        case variant o::none: break;
        }
    }
    array<usize> order = sorted_order(&keys, start);
    drop order;
    return value::map(move entries);
}

/* The value of the innermost container, which is removed from the stacks. */
protected value close_frame(decoding* state, bool strict) throws std.alloc::alloc_error, cbor_error {
    const u8 kind = take_u8(&state->kinds);
    const u64 count = take_u64(&state->counts);
    count as void;
    const u64 tag = take_u64(&state->tags);
    const usize start = take_usize(&state->starts);
    const usize key_start = take_usize(&state->key_starts);
    key_start as void;
    const usize previous_start = take_usize(&state->previous_starts);
    previous_start as void;
    const usize previous_end = take_usize(&state->previous_ends);
    previous_end as void;
    array<value> parts = take_parts(&state->parts);
    if (kind == 4u8) {
        tag as void;
        start as void;
        return value::sequence(move parts);
    }
    if (kind == 5u8) {
        tag as void;
        return map_value(move parts, start, strict);
    }
    start as void;
    value content = take_value(&parts);
    return value::tag_of(tag, move content);
}

/* Adds a finished item to the innermost container; in strict mode a map key shall follow the
   previous key of its map in the bytewise order of their encodings. */
protected void attach(decoding* state, value item, const u8[] input, usize end, bool strict)
    throws std.alloc::alloc_error, cbor_error {
    const usize top = len(state->kinds) - 1usize;
    const bool key = state->kinds[top] == 5u8 && len(state->parts[top]) % 2usize == 0usize;
    if (key == true && strict == true) {
        const usize key_start = state->key_starts[top];
        const usize previous_end = state->previous_ends[top];
        if (previous_end != 0usize &&
            key_before(input[state->previous_starts[top]..previous_end], input[key_start..end]) == false) {
            throw failure(error_code::not_deterministic, key_start);
        }
        state->previous_starts[top] = key_start;
        state->previous_ends[top] = end;
    }
    push_value(&state->parts[top], move item);
}

protected value decode_with(const u8[] input, bool strict) throws std.alloc::alloc_error, cbor_error {
    decoding state = decoding {
        .kinds = [], .counts = [], .tags = [], .starts = [], .parts = [], .key_starts = [],
        .previous_starts = [], .previous_ends = [],
    };
    usize at = 0usize;
    while (true) {
        if (len(state.kinds) > 0usize && frame_complete(&state, input, &at) == true) {
            value built = close_frame(&state, strict);
            if (len(state.kinds) == 0usize) {
                if (at != len(input)) { throw failure(error_code::trailing_data, at); }
                return move built;
            }
            attach(&state, move built, input, at, strict);
            continue;
        }
        const usize start = at;
        if (len(state.kinds) > 0usize) {
            const usize top = len(state.kinds) - 1usize;
            if (state.kinds[top] == 5u8 && len(state.parts[top]) % 2usize == 0usize) {
                state.key_starts[top] = start;
            }
        }
        const u8 major = (peek(input, at) >> 5u8) as u8;
        if (major == 4u8 || major == 5u8) {
            const u64 count = container_head(input, &at, strict);
            open_frame(&state, major, count, 0u64, start);
            continue;
        }
        if (major == 6u8) {
            const u64 tag = tag_head(input, &at, strict);
            open_frame(&state, 6u8, 1u64, tag, start);
            continue;
        }
        value scalar = decode_scalar(input, &at, strict);
        if (len(state.kinds) == 0usize) {
            if (at != len(input)) { throw failure(error_code::trailing_data, at); }
            return move scalar;
        }
        attach(&state, move scalar, input, at, strict);
    }
    panic("std.cbor: the decoder left its loop");
}

/* R-SLIB-CBOR-0005: the single data item that is the whole input, well-formed and valid. */
value decode(const u8[] input) throws std.alloc::alloc_error, cbor_error {
    return decode_with(input, false);
}

/* R-SLIB-CBOR-0005: the same for an input that shall be in deterministic encoding. */
value decode_deterministic(const u8[] input) throws std.alloc::alloc_error, cbor_error {
    return decode_with(input, true);
}

protected void push_job_only(array<u8>* jobs, u8 job) throws std.alloc::alloc_error {
    try {
        jobs->push(job);
    } catch (std.array::push_error<u8> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* ---- Diagnostic notation ---- */

protected const u8 SHOW_ITEM = 0u8;
protected const u8 SHOW_COMMA = 1u8;
protected const u8 SHOW_CLOSE_ARRAY = 2u8;
protected const u8 SHOW_CLOSE_MAP = 3u8;
protected const u8 SHOW_COLON = 4u8;
protected const u8 SHOW_CLOSE_TAG = 5u8;

protected str punctuation(u8 job) {
    if (job == SHOW_COMMA) { return ", "; }
    if (job == SHOW_CLOSE_ARRAY) { return "]"; }
    if (job == SHOW_CLOSE_MAP) { return "}"; }
    if (job == SHOW_COLON) { return ": "; }
    return ")";
}

protected u8 hex_digit(u8 number) {
    if (number < 10u8) { return (number + 48u8) as u8; }
    return (number + 87u8) as u8;
}

/* -1 - number in decimal; the most negative value -2^64 has no u64 magnitude. */
protected std.string::string negative_text(u64 number) throws std.alloc::alloc_error {
    if (number == 0xffffffffffffffffu64) { return std.string::from_str("-18446744073709551616"); }
    const u64 magnitude = number + 1u64;
    return f"-{magnitude}";
}

protected void append_quoted(std.string::string* out, const std.string::string* text)
    throws std.alloc::alloc_error {
    std.string::push_scalar(out, '"');
    const u8[] raw_bytes = *text;
    usize run = 0usize;
    for (usize index = 0usize; index <= len(raw_bytes); index += 1usize) {
        bool special = index == len(raw_bytes);
        if (special == false) {
            const u8 byte = raw_bytes[index];
            special = byte == 34u8 || byte == 92u8 || byte < 32u8 || byte == 127u8;
        }
        if (special == false) { continue; }
        if (index > run) {
            try {
                std.string::append_str(out, core::validate_utf8(raw_bytes[run..index]));
            } catch (core::utf8_error rejected) {
                rejected as void;
            }
        }
        run = index + 1usize;
        if (index == len(raw_bytes)) { break; }
        const u8 byte = raw_bytes[index];
        if (byte == 34u8 || byte == 92u8) {
            std.string::push_scalar(out, '\\');
            std.string::push_scalar(out, byte as char);
        } else {
            std.string::append_str(out, "\\u00");
            std.string::push_scalar(out, hex_digit((byte >> 4u8) as u8) as char);
            std.string::push_scalar(out, hex_digit((byte & 15u8) as u8) as char);
        }
    }
    std.string::push_scalar(out, '"');
}

protected void append_float_text(std.string::string* out, f64 number) throws std.alloc::alloc_error {
    if (std.math::is_nan_f64(number) == true) {
        std.string::append_str(out, "NaN");
        return;
    }
    if (std.math::is_infinite_f64(number) == true) {
        if (number < 0.0) {
            std.string::append_str(out, "-Infinity");
        } else {
            std.string::append_str(out, "Infinity");
        }
        return;
    }
    std.string::string text = f"{number}";
    const u8[] raw_bytes = text;
    bool marked = false;
    for (usize index = 0usize; index < len(raw_bytes); index += 1usize) {
        if (raw_bytes[index] == 46u8 || raw_bytes[index] == 101u8) { marked = true; }
    }
    std.string::append_str(out, text);
    if (marked == false) { std.string::append_str(out, ".0"); }
}

/* An item that is neither an array, a map nor a tag. */
protected void show_scalar(std.string::string* out, const value* item) throws std.alloc::alloc_error {
    switch (*item) {
    case variant value::unsigned(number):
        const u64 shown = *number;
        std.string::string text = f"{shown}";
        std.string::append_str(out, text);
    case variant value::negative(number):
        std.string::string text = negative_text(*number);
        std.string::append_str(out, text);
    case variant value::bytes(data):
        std.string::append_str(out, "h'");
        const u8[] raw_bytes = std.array::as_slice(data);
        for (usize index = 0usize; index < len(raw_bytes); index += 1usize) {
            std.string::push_scalar(out, hex_digit((raw_bytes[index] >> 4u8) as u8) as char);
            std.string::push_scalar(out, hex_digit((raw_bytes[index] & 15u8) as u8) as char);
        }
        std.string::push_scalar(out, '\'');
    case variant value::text(data): append_quoted(out, data);
    case variant value::simple(number):
        const u8 shown = *number;
        std.string::string text = f"simple({shown})";
        std.string::append_str(out, text);
    case variant value::boolean(truth):
        if (*truth == true) {
            std.string::append_str(out, "true");
        } else {
            std.string::append_str(out, "false");
        }
    case variant value::null_value: std.string::append_str(out, "null");
    case variant value::undefined: std.string::append_str(out, "undefined");
    case variant value::float(number): append_float_text(out, *number);
    default: std.string::append_str(out, "?");
    }
}

/* R-SLIB-CBOR-0006: the diagnostic notation of RFC 8949 section 8, with byte strings in base16
   and floats in the shortest decimal form that reads back exactly. */
protected std.string::string show_tree(const value* item)
    throws std.alloc::alloc_error, std.array::push_error<const value*> {
    std.string::string out = std.string::create();
    array<u8> jobs = [];
    array<const value*> pending = [];
    push_job_only(&jobs, SHOW_ITEM);
    pending.push(item);
    while (len(jobs) > 0usize) {
        const u8 job = take_u8(&jobs);
        o<const value*> popped = std.array::pop(&pending);
        if (job != SHOW_ITEM) {
            popped as void;
            std.string::append_str(&out, punctuation(job));
            continue;
        }
        switch (popped) {
        case variant o::some(found):
            const value* current = *found;
            switch (*current) {
            case variant value::sequence(elements):
                std.string::push_scalar(&out, '[');
                const value[] listed = std.array::as_slice(elements);
                push_job_only(&jobs, SHOW_CLOSE_ARRAY);
                pending.push(current);
                for (usize index = len(listed); index > 0usize; index -= 1usize) {
                    push_job_only(&jobs, SHOW_ITEM);
                    pending.push(&listed[index - 1usize]);
                    if (index > 1usize) {
                        push_job_only(&jobs, SHOW_COMMA);
                        pending.push(current);
                    }
                }
            case variant value::map(entries):
                std.string::push_scalar(&out, '{');
                const entry[] listed = std.array::as_slice(entries);
                push_job_only(&jobs, SHOW_CLOSE_MAP);
                pending.push(current);
                for (usize index = len(listed); index > 0usize; index -= 1usize) {
                    const entry* pair = &listed[index - 1usize];
                    push_job_only(&jobs, SHOW_ITEM);
                    pending.push(&pair->item);
                    push_job_only(&jobs, SHOW_COLON);
                    pending.push(current);
                    push_job_only(&jobs, SHOW_ITEM);
                    pending.push(&pair->key);
                    if (index > 1usize) {
                        push_job_only(&jobs, SHOW_COMMA);
                        pending.push(current);
                    }
                }
            case variant value::tagged(tagged):
                const u64 tag = tagged->tag;
                std.string::string text = f"{tag}(";
                std.string::append_str(&out, text);
                push_job_only(&jobs, SHOW_CLOSE_TAG);
                pending.push(current);
                push_job_only(&jobs, SHOW_ITEM);
                pending.push(&*tagged->content);
            default: show_scalar(&out, current);
            }
        case variant o::none: break;
        }
    }
    return move out;
}

/* R-SLIB-CBOR-0006: the diagnostic notation of RFC 8949 section 8, with byte strings in base16
   and floats in the shortest decimal form that reads back exactly. */
std.string::string diagnostic(const value* item) throws std.alloc::alloc_error {
    try {
        return show_tree(item);
    } catch (std.array::push_error<const value*> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}
