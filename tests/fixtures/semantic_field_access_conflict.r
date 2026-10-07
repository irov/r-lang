module test.semantic.field_access_conflict;

/* R-REFL-0006: the borrow is that of `&pair.field`; one field is not borrowed exclusively twice. */
struct Pair { u32 left; u32 right; };

i32 main() {
    Pair pair = {.left = 1u32, .right = 2u32};
    u32* first = core::field_mut(&pair, 0usize);
    u32* second = core::field_mut(&pair, 0usize);
    *first += 1u32;
    *second += 1u32;
    return 0;
}
