module test.codegen.async_core_integer_intrinsics;

async i32 main() {
    o<i64> checked = core::checked_mul_i64(6i64, 7i64);
    switch (checked) {
        case variant o::some(value): if (*value != 42i64) { return 1; } break;
        case variant o::none: return 2;
    }
    if (core::wrapping_add_u64(18446744073709551615u64, 1u64) != 0u64) { return 3; }
    if (core::saturating_add_u64(18446744073709551615u64, 1u64) !=
        18446744073709551615u64) { return 4; }
    return 0;
}
