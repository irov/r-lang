module test.codegen.ffi_wide_declarations;

/* R-LIMIT-0001, R-FFI-0042 (L14-N3): the ABI verifier and the bridge spell a C declaration of
   any length. The prototype below spells longer than two kilobytes in the header's names, and
   the bridge's private definition of the typedef struct names a member longer than sixty-three
   bytes. */
@link(name = "wide", kind = "static")
@header("wide_library.h")
@abi("wide-abi")
extern "C" {
    @repr(C)
    @c_type(name = "wide_record_with_a_long_name_with_a_long_name_with_a_long_name_with_a_long_name_with_a_long_name_with_a_long_name_with_a_long_name_with_a_long_name_with_a_long_name_with_a_long_name_with_a_long_name_t", kind = "typedef")
    struct WideRecord {
        c_int member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value;
    };

    @safety("WIDE-SUM", "Every argument points to a live record")
    c_int wide_sum(raw const WideRecord* a0, raw const WideRecord* a1, raw const WideRecord* a2, raw const WideRecord* a3, raw const WideRecord* a4, raw const WideRecord* a5, raw const WideRecord* a6, raw const WideRecord* a7, raw const WideRecord* a8, raw const WideRecord* a9, raw const WideRecord* a10, raw const WideRecord* a11);
}

i32 main() {
    WideRecord r0 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 1 as c_int};
    WideRecord r1 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 2 as c_int};
    WideRecord r2 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 3 as c_int};
    WideRecord r3 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 4 as c_int};
    WideRecord r4 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 5 as c_int};
    WideRecord r5 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 6 as c_int};
    WideRecord r6 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 7 as c_int};
    WideRecord r7 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 8 as c_int};
    WideRecord r8 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 9 as c_int};
    WideRecord r9 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 10 as c_int};
    WideRecord r10 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 11 as c_int};
    WideRecord r11 = WideRecord {.member_name_longer_than_sixty_three_bytes_for_the_bridge_private_definition_value = 12 as c_int};
    unsafe {
        i32 total = wide_sum(&r0 as raw const WideRecord*, &r1 as raw const WideRecord*, &r2 as raw const WideRecord*, &r3 as raw const WideRecord*, &r4 as raw const WideRecord*, &r5 as raw const WideRecord*, &r6 as raw const WideRecord*, &r7 as raw const WideRecord*, &r8 as raw const WideRecord*, &r9 as raw const WideRecord*, &r10 as raw const WideRecord*, &r11 as raw const WideRecord*) as i32;
        if (total != 78) {
            return 1;
        }
    }
    return 0;
}
