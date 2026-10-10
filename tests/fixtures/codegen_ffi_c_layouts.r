module test.codegen.ffi_c_layouts;

/* R-AGG-0014, R-FFI-0061, R-FFI-0062 (B9.3): C unions, packed and aligned structs and bit-fields
   declared from a header, proven against it and passed by value and through pointers. */
@link(name = "layouts", kind = "static")
@header("c_layouts.h")
@abi("layouts-abi")
extern "C" {
    @repr(C)
    @c_type(name = "layout_value", kind = "typedef")
    union LayoutValue {
        c_int integer;
        c_double real;
        c_uchar[3] bytes;
    };

    @repr(C, packed)
    @c_type(name = "layout_packed", kind = "struct")
    struct LayoutPacked {
        c_uchar tag;
        c_int count;
        c_ushort flags;
    };

    @repr(C, align = 16)
    @c_type(name = "layout_aligned", kind = "struct")
    struct LayoutAligned {
        c_int x;
    };

    @repr(C)
    @c_type(name = "layout_field_aligned", kind = "struct")
    struct LayoutFieldAligned {
        c_uchar tag;
        @align(8) c_int value;
    };

    @repr(C)
    @c_type(name = "layout_bits", kind = "struct")
    struct LayoutBits {
        c_uint a : 3;
        c_uint b : 5;
        c_int c : 7;
        c_uchar d : 2;
        c_ullong e : 40;
        c_bool f : 1;
        c_short g : 9;
    };

    @repr(C, packed)
    @c_type(name = "layout_packed_bits", kind = "struct")
    struct LayoutPackedBits {
        c_uchar a : 4;
        c_int b : 30;
        c_uchar c;
    };

    @safety("LAYOUT-VALUE", "The value holds a double")
    c_double layout_value_real(LayoutValue value);
    @safety("LAYOUT-VALUE-OF", "None")
    LayoutValue layout_value_of_integer(c_int integer);
    @safety("LAYOUT-PACKED", "None")
    c_int layout_packed_sum(LayoutPacked packed);
    @safety("LAYOUT-PACKED-MAKE", "None")
    LayoutPacked layout_packed_make(c_uchar tag, c_int count, c_ushort flags);
    @safety("LAYOUT-ALIGNED", "The pointer refers to a live value")
    c_int layout_aligned_get(raw const LayoutAligned* aligned);
    @safety("LAYOUT-FIELD-ALIGNED", "None")
    c_int layout_field_aligned_sum(LayoutFieldAligned value);
    @safety("LAYOUT-BITS", "None")
    c_llong layout_bits_sum(LayoutBits bits);
    @safety("LAYOUT-BITS-MAKE", "None")
    LayoutBits layout_bits_make();
    @safety("LAYOUT-PACKED-BITS", "The pointer refers to a live value")
    c_int layout_packed_bits_b(raw const LayoutPackedBits* bits);
}

i32 main() {
    LayoutValue value = LayoutValue { .real = 2.5f64 as c_double };
    LayoutPacked packed = LayoutPacked { .tag = 1u8 as c_uchar, .count = 1000i32 as c_int,
                                         .flags = 20u16 as c_ushort };
    LayoutAligned aligned = LayoutAligned { .x = 77i32 as c_int };
    LayoutFieldAligned field_aligned = LayoutFieldAligned { .tag = 3u8 as c_uchar,
                                                            .value = 40i32 as c_int };
    LayoutBits bits = LayoutBits { .a = 5u32 as c_uint, .b = 17u32 as c_uint,
                                   .c = -33i32 as c_int, .d = 3u8 as c_uchar,
                                   .e = 0xABCDE12345u64 as c_ullong, .f = 1u8 as c_bool,
                                   .g = -200i16 as c_short };
    LayoutPackedBits packed_bits = LayoutPackedBits { .a = 9u8 as c_uchar,
                                                      .b = -123456789i32 as c_int,
                                                      .c = 0x5au8 as c_uchar };
    unsafe {
        if (layout_value_real(value) != 2.5f64 as c_double) {
            return 1;
        }
        LayoutValue made = layout_value_of_integer(-7i32 as c_int);
        if (made.integer != -7i32 as c_int) {
            return 2;
        }
        if (layout_packed_sum(packed) != 1021i32 as c_int) {
            return 3;
        }
        LayoutPacked made_packed = layout_packed_make(4u8 as c_uchar, -9i32 as c_int,
                                                      65535u16 as c_ushort);
        if (made_packed.tag != 4u8 as c_uchar || made_packed.count != -9i32 as c_int ||
            made_packed.flags != 65535u16 as c_ushort) {
            return 4;
        }
        if (layout_aligned_get(&aligned as raw const LayoutAligned*) != 77i32 as c_int) {
            return 5;
        }
        if (layout_field_aligned_sum(field_aligned) != 43i32 as c_int) {
            return 6;
        }
        c_llong expected = (5i64 + 17i64 - 33i64 + 3i64 + 0xABCDE12345i64 + 1i64 - 200i64)
                               as c_llong;
        if (layout_bits_sum(bits) != expected) {
            return 7;
        }
        LayoutBits made_bits = layout_bits_make();
        if (made_bits.a != 5u32 as c_uint || made_bits.b != 17u32 as c_uint ||
            made_bits.c != -33i32 as c_int || made_bits.d != 3u8 as c_uchar ||
            made_bits.e != 0xABCDE12345u64 as c_ullong || made_bits.f != 1u8 as c_bool ||
            made_bits.g != -200i16 as c_short) {
            return 8;
        }
        if (layout_packed_bits_b(&packed_bits as raw const LayoutPackedBits*) !=
            -123456789i32 as c_int) {
            return 9;
        }
    }
    return 0;
}
