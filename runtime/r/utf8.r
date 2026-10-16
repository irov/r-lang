module runtime.utf8;

// UTF-8 validation and encoding of the runtime (r_runtime_utf8.h): the bytes of a well-formed
// UTF-8 sequence (Unicode 15, table 3-7), boundaries between scalars, and the encoding of one
// scalar value.
@header("r_runtime_utf8.h")
extern "C" {
}

protected bool continuation(c_uint8 byte) {
    u8 value = byte as u8;
    return (value >= 0x80u8) && (value <= 0xbfu8);
}

// The width of the well-formed sequence that starts at `index`, or 0 when it is ill-formed.
protected usize sequence_width(const c_uint8[] bytes, usize index) {
    u8 first = bytes[index] as u8;
    usize left = len(bytes) - index;
    if (first <= 0x7fu8) {
        return 1usize;
    }
    if ((first >= 0xc2u8) && (first <= 0xdfu8)) {
        if ((left >= 2usize) && (continuation(bytes[index + 1usize]) == true)) {
            return 2usize;
        }
        return 0usize;
    }
    if ((first >= 0xe0u8) && (first <= 0xefu8)) {
        if (left < 3usize) {
            return 0usize;
        }
        u8 second = bytes[index + 1usize] as u8;
        u8 low = 0x80u8;
        u8 high = 0xbfu8;
        if (first == 0xe0u8) {
            low = 0xa0u8;
        }
        if (first == 0xedu8) {
            high = 0x9fu8;
        }
        if ((second >= low) && (second <= high) && (continuation(bytes[index + 2usize]) == true)) {
            return 3usize;
        }
        return 0usize;
    }
    if ((first >= 0xf0u8) && (first <= 0xf4u8)) {
        if (left < 4usize) {
            return 0usize;
        }
        u8 second = bytes[index + 1usize] as u8;
        u8 low = 0x80u8;
        u8 high = 0xbfu8;
        if (first == 0xf0u8) {
            low = 0x90u8;
        }
        if (first == 0xf4u8) {
            high = 0x8fu8;
        }
        if ((second >= low) && (second <= high) && (continuation(bytes[index + 2usize]) == true) &&
            (continuation(bytes[index + 3usize]) == true)) {
            return 4usize;
        }
        return 0usize;
    }
    return 0usize;
}

@safety("R-RUNTIME-UTF8-VALIDATE", "bytes addresses length readable bytes or is null with length zero; invalid_index is null or writable")
extern "C" c_bool r_runtime_utf8_validate(raw const c_uint8*? bytes, c_size length, raw c_size*? invalid_index) {
    usize count = length as usize;
    unsafe {
        if ((bytes == null) && (count != 0usize)) {
            if (invalid_index != null) {
                *invalid_index = 0i32 as c_size;
            }
            return 0i32 as c_bool;
        }
        const c_uint8[] view = core::slice_from_raw_parts(bytes, count);
        usize index = 0usize;
        while (index < len(view)) {
            usize width = sequence_width(view, index);
            if (width == 0usize) {
                if (invalid_index != null) {
                    *invalid_index = index as c_size;
                }
                return 0i32 as c_bool;
            }
            index += width;
        }
        if (invalid_index != null) {
            *invalid_index = length;
        }
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-UTF8-BOUNDARY", "bytes addresses length readable bytes or is null with length zero")
extern "C" c_bool r_runtime_utf8_is_scalar_boundary(raw const c_uint8*? bytes, c_size length, c_size index) {
    usize count = length as usize;
    usize at = index as usize;
    unsafe {
        if ((at > count) || ((bytes == null) && (count != 0usize))) {
            return 0i32 as c_bool;
        }
        if ((at == 0usize) || (at == count)) {
            return 1i32 as c_bool;
        }
        const c_uint8[] view = core::slice_from_raw_parts(bytes, count);
        if (continuation(view[at]) == true) {
            return 0i32 as c_bool;
        }
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-UTF8-ENCODE", "output is null or addresses four writable bytes; length is null or writable")
extern "C" c_bool r_runtime_utf8_encode(c_uint32 scalar, raw c_uint8*? output, raw c_size*? length) {
    u32 value = scalar as u32;
    unsafe {
        if ((output == null) || (length == null) || (value > 0x10ffffu32) ||
            ((value >= 0xd800u32) && (value <= 0xdfffu32))) {
            return 0i32 as c_bool;
        }
        c_uint8[] bytes = core::slice_from_raw_parts_mut(output, 4usize);
        usize width = 4usize;
        if (value <= 0x7fu32) {
            bytes[0] = value as c_uint8;
            width = 1usize;
        } else {
            if (value <= 0x7ffu32) {
                bytes[0] = (0xc0u32 | (value >> 6u32)) as c_uint8;
                bytes[1] = (0x80u32 | (value & 0x3fu32)) as c_uint8;
                width = 2usize;
            } else {
                if (value <= 0xffffu32) {
                    bytes[0] = (0xe0u32 | (value >> 12u32)) as c_uint8;
                    bytes[1] = (0x80u32 | ((value >> 6u32) & 0x3fu32)) as c_uint8;
                    bytes[2] = (0x80u32 | (value & 0x3fu32)) as c_uint8;
                    width = 3usize;
                } else {
                    bytes[0] = (0xf0u32 | (value >> 18u32)) as c_uint8;
                    bytes[1] = (0x80u32 | ((value >> 12u32) & 0x3fu32)) as c_uint8;
                    bytes[2] = (0x80u32 | ((value >> 6u32) & 0x3fu32)) as c_uint8;
                    bytes[3] = (0x80u32 | (value & 0x3fu32)) as c_uint8;
                }
            }
        }
        *length = width as c_size;
    }
    return 1i32 as c_bool;
}
