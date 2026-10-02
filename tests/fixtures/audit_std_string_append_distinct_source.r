module test.audit.std_string_append_distinct_source;

i32 main() {
    try {
        std.string::string target = std.string::from_str("x");
        std.string::string text_source = std.string::from_str("abc");
        std.string::string utf8_source = std.string::from_utf8("z");
        std.string::append_str(&target, std.string::as_str(&text_source));
        std.string::append_utf8(&target, std.string::as_bytes(&utf8_source));
        if (std.string::len(&target) != 5 || std.string::len(&text_source) != 3 ||
            std.string::len(&utf8_source) != 1) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    } catch (std.string::string_error failure) {
        return 3;
    }
}
