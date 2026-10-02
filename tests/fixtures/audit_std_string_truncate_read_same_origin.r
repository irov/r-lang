module test.audit.std_string_truncate_read_same_origin;

i32 main() {
    try {
        std.string::string value = std.string::from_str("abc");
        std.string::truncate(&value, std.string::len(&value));
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    } catch (std.string::boundary_error failure) {
        return 3;
    }
}
