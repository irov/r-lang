module example.unzip.bytes;

import example.unzip.error::{ZipError, ZipErrorCode};

bool has_span(usize total, usize offset, usize width) {
    if (offset > total) {
        return false;
    }
    usize remaining = total - offset;
    if (width > remaining) {
        return false;
    }
    return true;
}

u16 read_u16_le(const u8[] source, usize offset) throws ZipError {
    usize size = len(source);
    bool available = has_span(size, offset, 2);
    throw (available == false) ZipError {
        .code = ZipErrorCode::Truncated,
        .offset = offset,
        .message = "truncated 16-bit field",
    };

    u16 low = source[offset] as u16;
    usize high_index = offset + 1;
    u16 high = source[high_index] as u16;
    u16 value = (low | (high << 8)) as u16;
    return value;
}

u32 read_u32_le(const u8[] source, usize offset) throws ZipError {
    usize size = len(source);
    bool available = has_span(size, offset, 4);
    throw (available == false) ZipError {
        .code = ZipErrorCode::Truncated,
        .offset = offset,
        .message = "truncated 32-bit field",
    };

    usize i1 = offset + 1;
    usize i2 = offset + 2;
    usize i3 = offset + 3;
    u32 b0 = source[offset] as u32;
    u32 b1 = source[i1] as u32;
    u32 b2 = source[i2] as u32;
    u32 b3 = source[i3] as u32;
    u32 value = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
    return value;
}

bool ranges_equal(const u8[] left, const u8[] right) {
    usize left_size = len(left);
    usize right_size = len(right);
    if (left_size != right_size) {
        return false;
    }

    usize index = 0;
    while (index < left_size) {
        if (left[index] != right[index]) {
            return false;
        }
        index += 1;
    }
    return true;
}
