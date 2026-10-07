module test.semantic.field_access_range;

/* R-REFL-0006: the field index of core::field is below the field count. */
struct Pair { u32 left; u32 right; };

i32 main() {
    Pair pair = {.left = 1u32, .right = 2u32};
    const u32* value = core::field(&pair, 2usize);
    return 0;
}
