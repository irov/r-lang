module test.audit.std_string_append_utf8_self_alias;

i32 main() {
    try {
        std.string::string value =
            std.string::from_utf8("abcdefghijklmnopqrstuvwxyz0123456789");
        std.string::append_utf8(&value, std.string::as_bytes(&value));
        return 0;
    } catch (std.string::string_error failure) {
        return 2;
    }
}
