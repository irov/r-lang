module test.codegen.async_field_destructuring;

/* R-STMT-0022 (L40) in async bodies: the payloads of std.fs outcomes are taken apart, so a
   directory is walked to its end with the iterator each entry returns, and a refused atomic
   write is retried with the data it handed back. */

protected bytes letter(u8 value) throws std.alloc::alloc_error {
    bytes made = {};
    std.bytes::append_u8(&made, value);
    return move made;
}

protected std.fs::path in_folder(const std.fs::path* folder, str name) throws std.fs::path_error, std.alloc::alloc_error {
    std.fs::path leaf = std.fs::path_from_utf8(name);
    return std.fs::path_join(folder, &leaf);
}

async i32 main(const str[] args) {
    if (len(args) != 3usize) { return 30; }
    try {
        str root_text = args[1];
        str relative_text = args[2];
        std.fs::path root = std.fs::path_from_utf8(root_text);
        std.fs::path relative = std.fs::path_from_utf8(relative_text);
        std.fs::path folder = std.fs::path_join(&root, &relative);
        await std.fs::create_directory(&folder, true, o::none);
        std.fs::path a = in_folder(&folder, "a");
        std.fs::path b = in_folder(&folder, "b");
        bytes for_a = letter(65u8);
        std.fs::write_file_result first = await std.fs::write_file_atomic_no_replace(&a, move for_a, o::none);
        drop first;
        bytes for_b = letter(66u8);
        std.fs::write_file_result second = await std.fs::write_file_atomic_no_replace(&b, move for_b, o::none);
        drop second;

        /* A write that is refused hands the data back; it is published under another name. */
        bytes again_a = letter(90u8);
        std.fs::write_file_result refused = await std.fs::write_file_atomic_no_replace(&a, move again_a, o::none);
        switch (move refused) {
        case variant std.fs::write_file_result::committed(move kept):
            drop kept;
            return 1;
        case variant std.fs::write_file_result::failed(move failure):
            auto {.error, .data} = move failure;
            if (error.code != std.fs::error_code::already_exists || len(data) != 1usize) { return 2; }
            std.fs::path c = in_folder(&folder, "c");
            std.fs::write_file_result retried = await std.fs::write_file_atomic_no_replace(&c, move data, o::none);
            switch (move retried) {
            case variant std.fs::write_file_result::committed(move kept): drop kept;
            case variant std.fs::write_file_result::failed(move again):
                drop again;
                return 3;
            }
        }

        std.fs::directory opened = await std.fs::open_directory(&folder, o::none);
        std.fs::directory_iter iterator = await std.fs::iterate(&opened, o::none);
        u32 regular = 0u32;
        walk: while (true) {
            std.fs::directory_next_result result = await std.fs::next(move iterator, o::none);
            switch (move result) {
            case variant std.fs::directory_next_result::entry(move item):
                auto {.iterator = rest, .entry} = move item;
                iterator = move rest;
                if (entry.kind == std.fs::file_kind::regular) { regular += 1u32; }
            case variant std.fs::directory_next_result::end: break walk;
            case variant std.fs::directory_next_result::failed(move failure):
                auto {.error, .iterator = rest} = move failure;
                drop rest;
                throw error;
            }
        }
        if (regular != 3u32) { return 4; }
        return 0;
    } catch (std.error::fault failure) {
        failure as void;
    }
    return 99;
}
