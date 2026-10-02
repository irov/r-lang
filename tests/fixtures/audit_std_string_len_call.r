module audit.std_string_len_call;

i32 main() {
    try {
        std.string::string value = std.string::with_capacity(8usize);
        std.string::reserve(&value, 4usize);
        std.string::append_str(&value, "ab");
        std.string::push_scalar(&value, 'C');
        if (std.string::len(&value) != 3usize) {
            drop value;
            return 1;
        }
        if (std.string::capacity(&value) < std.string::len(&value)) {
            drop value;
            return 2;
        }
        std.string::clear(&value);
        if (std.string::len(&value) != 0usize) {
            drop value;
            return 3;
        }
        drop value;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 4;
    }

    try {
        std.string::string value = std.string::from_utf8("abcd");
        std.string::append_utf8(&value, "ef");
        std.string::truncate(&value, 4usize);
        const u8[] bytes = std.string::as_bytes(&value);
        if ((len(bytes) != 4usize) || (bytes[0] != 97u8) || (bytes[3] != 100u8)) {
            drop value;
            return 5;
        }
        std.string::truncate(&value, 5usize);
        drop value;
        return 6;
    } catch (std.string::string_error failure) {
        failure as void;
        return 7;
    } catch (std.string::boundary_error failure) {
        failure as void;
    }
    return 0;
}
