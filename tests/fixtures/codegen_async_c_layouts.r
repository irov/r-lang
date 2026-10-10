module test.codegen.async_c_layouts;

/* R-AGG-0014, R-FFI-0061, R-FFI-0062 (B9.3): unions, packed structs and bit-fields held in an
   async frame across awaits and passed to and from async functions. */

@repr(C)
union Value {
    c_int integer;
    c_double real;
};

@repr(C, packed)
struct Packed {
    c_uchar a;
    c_int b;
    c_ushort c;
};

@repr(C)
struct Bits {
    c_uint a : 3;
    c_int c : 7;
    c_ullong e : 40;
    c_short g : 9;
};

protected async c_int widen(c_int value) {
    return value + (1i32 as c_int);
}

protected async Bits shifted(Bits bits) throws std.async::start_error {
    Bits result = bits;
    result.c = await widen(result.c);
    result.a = 7u32 as c_uint;
    return result;
}

protected async i32 run() throws std.async::start_error {
    Value value = Value { .integer = 41i32 as c_int };
    Packed packed = Packed { .a = 1u8 as c_uchar, .b = 1000i32 as c_int, .c = 2u16 as c_ushort };
    Bits bits = Bits { .a = 1u32 as c_uint, .c = -20i32 as c_int, .e = 0x123456789Au64 as c_ullong,
                       .g = -100i16 as c_short };
    unsafe {
        value.integer = await widen(value.integer);
        if (value.integer != 42i32 as c_int) {
            return 1;
        }
    }
    packed.b = await widen(packed.b);
    if (packed.b != 1001i32 as c_int || packed.a != 1u8 as c_uchar || packed.c != 2u16 as c_ushort) {
        return 2;
    }
    Bits next = await shifted(bits);
    if (next.c != -19i32 as c_int || next.a != 7u32 as c_uint ||
        next.e != 0x123456789Au64 as c_ullong || next.g != -100i16 as c_short) {
        return 3;
    }
    bits.g = await widen(-256i32 as c_int) as c_short;
    if (bits.g != -255i16 as c_short || bits.c != -20i32 as c_int) {
        return 4;
    }
    return 0;
}

async i32 main() {
    try {
        return await run();
    } catch (std.async::start_error failure) {
        failure as void;
        return 9;
    }
}
