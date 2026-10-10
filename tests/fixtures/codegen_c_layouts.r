module test.codegen.c_layouts;

/* R-AGG-0014, R-FFI-0061, R-FFI-0062 (B9.3): C unions, packed and aligned aggregates and
   bit-fields have the layout the target C compiler gives the equivalent declarations (the sizes,
   offsets and bytes below are clang's), and their fields are read and written in place. */

@repr(C)
union Value {
    c_int integer;
    c_double real;
    c_uchar[3] bytes;
};

@repr(C, packed)
struct Packed {
    c_uchar a;
    c_int b;
    c_ushort c;
};

@repr(C, align = 16)
struct Aligned {
    c_int x;
};

@repr(C)
struct FieldAligned {
    c_uchar a;
    @align(8) c_int b;
};

@repr(C)
struct Bits {
    c_uint a : 3;
    c_uint b : 5;
    c_int c : 7;
    c_uchar d : 2;
    c_ullong e : 40;
    c_bool f : 1;
    c_short g : 9;
};

@repr(C, packed)
struct PackedBits {
    c_uchar a : 4;
    c_int b : 30;
    c_uchar c;
};

@repr(C, packed)
struct Nested {
    c_uchar a;
    FieldAligned inner;
};

@repr(C, packed)
union PackedUnion {
    c_int i;
    c_uchar[5] b;
};

@repr(C)
union BitsView {
    Bits value;
    c_uchar[16] bytes;
};

@repr(C)
union PackedBitsView {
    PackedBits value;
    c_uchar[6] bytes;
};

/* Static objects are laid out during translation; a bit-field value is inserted at its bits. */
const Bits FIXED = Bits { .a = 5u32 as c_uint, .c = -20i32 as c_int, .e = 0x123456789Au64 as c_ullong,
                          .g = -256i16 as c_short };
const Packed PACKED = Packed { .a = 3u8 as c_uchar, .b = 77777i32 as c_int, .c = 9u16 as c_ushort };
const Value VALUE = Value { .real = 2.5f64 as c_double };
Bits counter = Bits { .a = 1u32 as c_uint, .c = 3i32 as c_int, .e = 9u64 as c_ullong };

c_int integer_of(Value value) {
    unsafe {
        return value.integer;
    }
}

i32 unions() {
    Value first = Value { .integer = 7i32 as c_int };
    Value zero = Value {};
    Value copy = first;
    if (sizeof(Value) != 8usize || alignof(Value) != 8usize) {
        return 1;
    }
    unsafe {
        if (copy.integer != 7i32 as c_int || zero.real != 0.0f64 as c_double) {
            return 2;
        }
        copy.real = 1.5f64 as c_double;
        if (copy.real != 1.5f64 as c_double) {
            return 3;
        }
        copy.bytes[0] = 9u8 as c_uchar;
        if (copy.bytes[0] != 9u8 as c_uchar) {
            return 4;
        }
    }
    if (integer_of(first) != 7i32 as c_int) {
        return 5;
    }
    return 0;
}

i32 layouts() {
    if (sizeof(Packed) != 7usize || alignof(Packed) != 1usize) {
        return 10;
    }
    if (sizeof(Aligned) != 16usize || alignof(Aligned) != 16usize) {
        return 11;
    }
    if (sizeof(FieldAligned) != 16usize || alignof(FieldAligned) != 8usize) {
        return 12;
    }
    if (sizeof(Bits) != 16usize || alignof(Bits) != 8usize) {
        return 13;
    }
    if (sizeof(PackedBits) != 6usize || alignof(PackedBits) != 1usize) {
        return 14;
    }
    if (sizeof(Nested) != 17usize || alignof(Nested) != 1usize) {
        return 15;
    }
    if (sizeof(PackedUnion) != 5usize || alignof(PackedUnion) != 1usize) {
        return 16;
    }
    return 0;
}

i32 packed_fields() {
    Packed packed = Packed { .a = 1u8 as c_uchar, .b = 0x01020304i32 as c_int,
                             .c = 0x0506u16 as c_ushort };
    packed.b = packed.b + (1i32 as c_int);
    if (packed.b != 0x01020305i32 as c_int || packed.c != 0x0506u16 as c_ushort) {
        return 20;
    }
    Nested nested = Nested { .a = 7u8 as c_uchar,
                             .inner = FieldAligned { .a = 2u8 as c_uchar, .b = 99i32 as c_int } };
    if (nested.inner.b != 99i32 as c_int) {
        return 21;
    }
    nested.inner.b = 100i32 as c_int;
    if (nested.inner.b != 100i32 as c_int || nested.a != 7u8 as c_uchar) {
        return 22;
    }
    return 0;
}

i32 bit_fields() {
    Bits stored = Bits {};
    stored.a = 5u32 as c_uint;
    stored.b = 17u32 as c_uint;
    stored.c = -33i32 as c_int;
    stored.d = 3u8 as c_uchar;
    stored.e = 0xABCDE12345u64 as c_ullong;
    stored.f = 1u8 as c_bool;
    stored.g = -200i16 as c_short;
    if (stored.a != 5u32 as c_uint || stored.b != 17u32 as c_uint || stored.c != -33i32 as c_int) {
        return 30;
    }
    if (stored.d != 3u8 as c_uchar || stored.e != 0xABCDE12345u64 as c_ullong ||
        stored.f != 1u8 as c_bool || stored.g != -200i16 as c_short) {
        return 31;
    }
    Bits built = Bits { .a = 5u32 as c_uint, .b = 17u32 as c_uint, .c = -33i32 as c_int,
                        .d = 3u8 as c_uchar, .e = 0xABCDE12345u64 as c_ullong,
                        .f = 1u8 as c_bool, .g = -200i16 as c_short };
    BitsView stored_view = BitsView { .value = stored };
    BitsView built_view = BitsView { .value = built };
    PackedBits packed = PackedBits { .a = 9u8 as c_uchar, .b = -123456789i32 as c_int,
                                     .c = 0x5au8 as c_uchar };
    PackedBitsView packed_view = PackedBitsView { .value = packed };
    unsafe {
        c_uchar[16] bytes = stored_view.bytes;
        c_uchar[16] same = built_view.bytes;
        c_uchar[6] packed_bytes = packed_view.bytes;
        for (usize i in 0usize..16usize) {
            if (bytes[i] != same[i]) {
                return 32;
            }
        }
        /* clang: 8d 5f 17 8d 84 37 af 06 38 01 00 ... and b9 2e 43 8a 03 5a. */
        if (bytes[0] != 0x8du8 as c_uchar || bytes[1] != 0x5fu8 as c_uchar ||
            bytes[2] != 0x17u8 as c_uchar || bytes[7] != 0x06u8 as c_uchar ||
            bytes[8] != 0x38u8 as c_uchar || bytes[9] != 0x01u8 as c_uchar) {
            return 33;
        }
        if (packed_bytes[0] != 0xb9u8 as c_uchar || packed_bytes[1] != 0x2eu8 as c_uchar ||
            packed_bytes[3] != 0x8au8 as c_uchar || packed_bytes[4] != 0x03u8 as c_uchar ||
            packed_bytes[5] != 0x5au8 as c_uchar) {
            return 34;
        }
    }
    stored.c = stored.c - (30i32 as c_int);
    if (stored.c != -63i32 as c_int || stored.b != 17u32 as c_uint || stored.d != 3u8 as c_uchar) {
        return 35;
    }
    return 0;
}

i32 statics() {
    Bits fixed = FIXED;
    if (fixed.a != 5u32 as c_uint || fixed.c != -20i32 as c_int ||
        fixed.e != 0x123456789Au64 as c_ullong || fixed.g != -256i16 as c_short ||
        fixed.b != 0u32 as c_uint) {
        return 40;
    }
    if (PACKED.b != 77777i32 as c_int || PACKED.a != 3u8 as c_uchar || PACKED.c != 9u16 as c_ushort) {
        return 41;
    }
    unsafe {
        if (VALUE.real != 2.5f64 as c_double) {
            return 42;
        }
        counter.c = counter.c + (1i32 as c_int);
        if (counter.c != 4i32 as c_int || counter.e != 9u64 as c_ullong || counter.a != 1u32 as c_uint) {
            return 43;
        }
    }
    return 0;
}

i32 main() {
    i32 outcome = unions();
    if (outcome == 0) {
        outcome = layouts();
    }
    if (outcome == 0) {
        outcome = packed_fields();
    }
    if (outcome == 0) {
        outcome = bit_fields();
    }
    if (outcome == 0) {
        outcome = statics();
    }
    return outcome;
}
