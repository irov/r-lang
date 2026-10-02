module test.audit.async_std_string_append_str_self_alias;

async i32 main() {
    try {
        std.string::string value =
            std.string::from_str("abcdefghijklmnopqrstuvwxyz0123456789");
        std.string::append_str(&value, std.string::as_str(&value));
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    }
}
