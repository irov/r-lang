module std.bytes;

/* R-SLIB-BYTES-0003: the R part of std.bytes, loaded by `import std.bytes;`. A cursor is a byte
   position; each read or write of an unsigned integer takes its bytes at that position, in
   little-endian (_le) or big-endian (_be) order, and moves the position past them. When fewer
   bytes remain the operation reports out_of_bounds and changes nothing. `{}` is the cursor at
   position zero. */
struct cursor { usize position; };

protected u64 read_unsigned(cursor* this, const u8[] source, usize width, bool big)
    throws std.bytes::bytes_error {
    usize at = this->position;
    throw (at > len(source) || len(source) - at < width) std.bytes::bytes_error::out_of_bounds;
    u64 value = 0u64;
    for (usize index = 0usize; index < width; index += 1usize) {
        usize from = at + index;
        if (big == false) { from = at + width - 1usize - index; }
        value = (value << 8u64) | (source[from] as u64);
    }
    this->position = at + width;
    return value;
}

protected void write_unsigned(cursor* this, u8[] target, usize width, bool big, u64 value)
    throws std.bytes::bytes_error {
    usize at = this->position;
    throw (at > len(target) || len(target) - at < width) std.bytes::bytes_error::out_of_bounds;
    for (usize index = 0usize; index < width; index += 1usize) {
        usize shift = index;
        if (big == true) { shift = width - 1usize - index; }
        target[at + index] = ((value >> ((shift * 8usize) as u64)) & 255u64) as u8;
    }
    this->position = at + width;
}

u8 cursor::read_u8(cursor* this, const u8[] source) throws std.bytes::bytes_error {
    return read_unsigned(this, source, 1usize, false) as u8;
}

u16 cursor::read_u16_le(cursor* this, const u8[] source) throws std.bytes::bytes_error {
    return read_unsigned(this, source, 2usize, false) as u16;
}

u16 cursor::read_u16_be(cursor* this, const u8[] source) throws std.bytes::bytes_error {
    return read_unsigned(this, source, 2usize, true) as u16;
}

u32 cursor::read_u32_le(cursor* this, const u8[] source) throws std.bytes::bytes_error {
    return read_unsigned(this, source, 4usize, false) as u32;
}

u32 cursor::read_u32_be(cursor* this, const u8[] source) throws std.bytes::bytes_error {
    return read_unsigned(this, source, 4usize, true) as u32;
}

u64 cursor::read_u64_le(cursor* this, const u8[] source) throws std.bytes::bytes_error {
    return read_unsigned(this, source, 8usize, false);
}

u64 cursor::read_u64_be(cursor* this, const u8[] source) throws std.bytes::bytes_error {
    return read_unsigned(this, source, 8usize, true);
}

void cursor::write_u8(cursor* this, u8[] target, u8 value) throws std.bytes::bytes_error {
    write_unsigned(this, target, 1usize, false, value as u64);
}

void cursor::write_u16_le(cursor* this, u8[] target, u16 value) throws std.bytes::bytes_error {
    write_unsigned(this, target, 2usize, false, value as u64);
}

void cursor::write_u16_be(cursor* this, u8[] target, u16 value) throws std.bytes::bytes_error {
    write_unsigned(this, target, 2usize, true, value as u64);
}

void cursor::write_u32_le(cursor* this, u8[] target, u32 value) throws std.bytes::bytes_error {
    write_unsigned(this, target, 4usize, false, value as u64);
}

void cursor::write_u32_be(cursor* this, u8[] target, u32 value) throws std.bytes::bytes_error {
    write_unsigned(this, target, 4usize, true, value as u64);
}

void cursor::write_u64_le(cursor* this, u8[] target, u64 value) throws std.bytes::bytes_error {
    write_unsigned(this, target, 8usize, false, value);
}

void cursor::write_u64_be(cursor* this, u8[] target, u64 value) throws std.bytes::bytes_error {
    write_unsigned(this, target, 8usize, true, value);
}
