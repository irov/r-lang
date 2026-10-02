module test.audit.std_string_reserve_read_same_origin;

i32 main() {
    try {
        std.string::string value = std.string::from_str("abc");
        std.string::reserve(&value, std.string::len(&value));
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    }
}
