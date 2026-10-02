module test.audit.alloc_new_error_catch_field_compiler_uaf;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


i32 main() {
    try {
        own i32* value = new i32(11);
        own (own i32*)* stored = std.alloc::try_new(move value);
        test_observe(&stored);
        return 1;
    } catch (std.alloc::new_error<own i32*> failure) {
        return *(failure.value);
    }
}
