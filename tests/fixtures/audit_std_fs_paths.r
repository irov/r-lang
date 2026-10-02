module audit.std_fs_paths;

i32 sync_probe() throws std.fs::path_error, std.alloc::alloc_error {
    std.fs::path base = std.fs::path_from_utf8("root");
    std.fs::path component = std.fs::path_from_utf8("child");
    std.fs::path duplicate = std.fs::path_clone(&base);
    std.fs::path joined = std.fs::path_join(&duplicate, &component);
    std.string::string text = std.fs::path_to_utf8(&joined);
    if (std.string::len(&text) != 10usize) {
        drop text;
        drop joined;
        drop duplicate;
        drop component;
        drop base;
        return 1;
    }
    drop text;
    drop joined;
    drop duplicate;
    drop component;
    drop base;
    return 0;
}

async i32 main() {
    try {
        if (sync_probe() != 0) {
            return 1;
        }

        std.fs::path base = std.fs::path_from_utf8("archive");
        std.fs::path component = std.fs::path_from_utf8("entry");
        std.fs::path duplicate = std.fs::path_clone(&base);
        std.fs::path joined = std.fs::path_join(&duplicate, &component);
        std.string::string text = std.fs::path_to_utf8(&joined);
        if (std.string::len(&text) != 13usize) {
            return 2;
        }
        drop text;
        drop joined;
        drop duplicate;
        drop component;
        drop base;

        std.fs::path relative_base = std.fs::path_from_utf8("safe");
        std.fs::path absolute_component = std.fs::path_from_utf8("/escape");
        try {
            std.fs::path invalid =
                std.fs::path_join(&relative_base, &absolute_component);
            drop invalid;
            return 3;
        } catch (std.fs::path_error failure) {
            failure as void;
        }
        drop absolute_component;
        drop relative_base;
        return 0;
    } catch (std.fs::path_error failure) {
        failure as void;
        return 4;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 5;
    }
}
