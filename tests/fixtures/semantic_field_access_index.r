module test.semantic.field_access_index;

/* R-REFL-0006: the field index of core::field is a translation-time constant. */
struct Pair { u32 left; u32 right; };

i32 main() {
    Pair pair = {.left = 1u32, .right = 2u32};
    usize chosen = 1usize;
    const u32* value = core::field(&pair, chosen);
    return *value as i32;
}
