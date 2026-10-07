module test.semantic.field_access_shared;

/* R-REFL-0006: core::field_mut takes an exclusive pointer. */
struct Pair { u32 left; u32 right; };

void reset(const Pair* pair) {
    u32* value = core::field_mut(pair, 0usize);
    *value = 0u32;
}

i32 main() { return 0; }
