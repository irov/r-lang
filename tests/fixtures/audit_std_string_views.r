module audit.std_string_views;

i32 sync_probe() throws std.alloc::alloc_error {
    std.string::string observed = std.string::from_str("alpha");
    str view = std.string::as_str(&observed);
    if (len(view) != 5usize) {
        drop observed;
        return 1;
    }
    drop observed;

    std.string::string consumed = std.string::from_str("bytes");
    bytes result = std.string::into_bytes(move consumed);
    if (len(result) != 5usize) {
        drop result;
        return 2;
    }
    std.string::from_bytes_result converted = std.string::from_bytes(move result);
    drop converted;
    return 0;
}

async i32 main() {
    try {
        if (sync_probe() != 0) {
            return 1;
        }
        std.string::string observed = std.string::from_str("async");
        str view = std.string::as_str(&observed);
        if (len(view) != 5usize) {
            return 2;
        }
        drop observed;

        std.string::string consumed = std.string::from_str("owner");
        bytes result = std.string::into_bytes(move consumed);
        if (len(result) != 5usize) {
            return 3;
        }
        std.string::from_bytes_result converted = std.string::from_bytes(move result);
        drop converted;
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 4;
    }
}
