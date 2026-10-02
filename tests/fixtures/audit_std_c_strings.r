module audit.std_c_strings;

i32 inspect_text(str text) throws std.c::string_error {
    std.c::c_string owned = std.c::string_from_str(text);
    const c_char[] storage = std.c::string_as_slice(&owned);
    if (len(storage) != len(text) + 1usize) {
        drop owned;
        return 1;
    }

    raw const c_char* pointer = std.c::string_as_ptr(&owned);
    pointer as void;

    str validated = std.c::validate_utf8(storage);
    if (len(validated) != len(text)) {
        drop owned;
        return 2;
    }

    std.string::string copied = std.c::copy_utf8(storage);
    str copied_view = std.string::as_str(&copied);
    if (len(copied_view) != len(text)) {
        drop copied;
        drop owned;
        return 3;
    }
    drop copied;
    drop owned;
    return 0;
}

bool rejects_embedded_nul() {
    try {
        std.c::c_string invalid = std.c::string_from_str("bad\0value");
        drop invalid;
        return false;
    } catch (std.c::string_error failure) {
        failure as void;
        return true;
    }
}

async i32 main() {
    try {
        if (inspect_text("sync") != 0) {
            return 1;
        }

        std.c::c_string owned = std.c::string_from_str("async");
        const c_char[] storage = std.c::string_as_slice(&owned);
        str validated = std.c::validate_utf8(storage);
        if (len(validated) != 5usize) {
            drop owned;
            return 2;
        }
        std.string::string copied = std.c::copy_utf8(storage);
        if (std.string::len(&copied) != 5usize) {
            drop copied;
            drop owned;
            return 3;
        }
        drop copied;
        drop owned;

        if (rejects_embedded_nul() == false) {
            return 4;
        }
        return 0;
    } catch (std.c::string_error failure) {
        failure as void;
        return 5;
    }
}
