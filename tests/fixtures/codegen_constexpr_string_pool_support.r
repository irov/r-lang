module test.codegen.constexpr_string_pool_support;

i32 support_use() {
    constexpr str shared = "pool-shared";
    constexpr str first = "a";
    constexpr str prefix_extension = "aa";
    constexpr str later_byte = "b";
    usize shared_length = len(shared);
    usize first_length = len(first);
    usize prefix_extension_length = len(prefix_extension);
    usize later_byte_length = len(later_byte);
    if (shared_length == 11) {
        if (first_length == 1) {
            if (prefix_extension_length == 2) {
                if (later_byte_length == 1) {
                    return 0;
                }
            }
        }
    }
    return 1;
}
