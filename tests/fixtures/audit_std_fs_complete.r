module audit.std_fs_complete;

bool verifies_write_start_failure(const std.fs::file* file, bytes buffer) {
    try {
        task<std.io::write_result> operation = std.fs::write(file, move buffer, o::none);
        std.async::cancel(move operation);
        return false;
    } catch (std.async::start_error error) {
        error as void;
        bool retained = len(buffer) == 1 && std.hash::crc32(buffer) == std.hash::crc32("F");
        return retained;
    }
}

bool is_entry(std.fs::directory_next_result result) {
    switch (move result) {
        case variant std.fs::directory_next_result::entry(move payload):
            bool regular = payload.entry.kind == std.fs::file_kind::regular;
            drop payload;
            return regular;
        case variant std.fs::directory_next_result::end:
            return false;
        case variant std.fs::directory_next_result::failed(move failure):
            drop failure;
            return false;
    }
}

async i32 main(const str[] args) {
    if (len(args) != 2) {
        return 1;
    }
    try {
        str root_text = args[1];
        std.fs::path root_path = std.fs::path_from_utf8(root_text);
        std.fs::path sub = std.fs::path_from_utf8("sub");
        std.fs::path sub_path = std.fs::path_join(&root_path, &sub);
        await std.fs::create_directory(&sub_path, false, o::none);
        std.fs::directory root = await std.fs::open_directory(&root_path, o::none);
        std.fs::directory directory = await std.fs::open_directory_beneath(&root, &sub, o::none);
        std.fs::metadata path_info = await std.fs::metadata(&sub_path, o::none);
        path_info as void;
        std.fs::metadata relative_info = await std.fs::metadata_beneath(&root, &sub, o::none);
        relative_info as void;
        std.fs::path file_name = std.fs::path_from_utf8("data");
        std.fs::open_file_options options = std.fs::open_file_options {
            .access = std.fs::access::read_write,
            .create = std.fs::create_mode::create_new,
            .truncate = false,
            .append = false,
            .follow_final_symlink = false,
        };
        std.fs::file file = await std.fs::open_file_beneath(&directory, &file_name, options, o::none);
        bytes preserved = {};
        std.bytes::append_u8(&preserved, 70);
        if (verifies_write_start_failure(&file, move preserved) == false) {
            return 16;
        }
        bytes first = {};
        std.bytes::append_u8(&first, 65);
        std.io::write_result written = await std.fs::write(&file, move first, o::none);
        drop written;
        bytes second = {};
        std.bytes::append_u8(&second, 66);
        std.io::write_all_result all_written = await std.fs::write_all(&file, move second, o::none);
        drop all_written;
        await std.fs::flush(&file, o::none);
        await std.fs::sync(&file, std.fs::sync_level::barrier, o::none);
        await std.fs::sync(&file, std.fs::sync_level::device, o::none);
        await std.fs::sync(&file, std.fs::sync_level::media, o::none);
        u64 position = await std.fs::seek(&file, std.fs::seek_origin::start, 0, o::none);
        if (position != 0) {
            return 2;
        }
        bytes buffer = {};
        std.bytes::append_u8(&buffer, 0);
        std.bytes::append_u8(&buffer, 0);
        try {
            task<std.io::read_result> operation = std.fs::read(&file, move buffer, o::none);
            std.async::cancel(move operation);
            return 17;
        } catch (std.async::start_error error) {
            error as void;
            if (len(buffer) != 2) { return 18; }
        }
        std.io::read_result read = await std.fs::read(&file, move buffer, o::none);
        switch (move read) {
            case variant std.io::read_result::read(move payload):
                const u8[] read_bytes = std.array::as_slice(&payload.buffer);
                if (payload.count != 2 || read_bytes[0] != 65 || read_bytes[1] != 66) {
                    return 3;
                }
                drop payload;
                break;
            case variant std.io::read_result::end(move returned):
                drop returned;
                return 4;
            case variant std.io::read_result::failed(move failure):
                drop failure;
                return 5;
        }
        try {
            await std.fs::close_file(move file, o::none);
            return 19;
        } catch (std.async::start_error error) {
            error as void;
        }
        await std.fs::close_file(move file, o::none);
        bytes contents = await std.fs::read_file_beneath(&directory, &file_name, 32, o::none);
        if (len(contents) != 2 || std.hash::crc32(contents) != std.hash::crc32("AB")) {
            return 6;
        }
        std.fs::path atomic_name = std.fs::path_from_utf8("atomic");
        std.fs::path atomic_path = std.fs::path_join(&sub_path, &atomic_name);
        std.fs::write_file_result published = await std.fs::write_file_atomic_no_replace(
            &atomic_path, move contents, o::none);
        switch (move published) {
            case variant std.fs::write_file_result::committed(move returned):
                drop returned;
                break;
            case variant std.fs::write_file_result::failed(move failure):
                drop failure;
                return 7;
        }
        bytes replacement = {};
        std.bytes::append_u8(&replacement, 99);
        std.fs::write_file_result duplicate = await std.fs::write_file_atomic_no_replace(
            &atomic_path, move replacement, o::none);
        switch (move duplicate) {
            case variant std.fs::write_file_result::committed(move unexpected):
                drop unexpected;
                return 23;
            case variant std.fs::write_file_result::failed(move failure):
                if (failure.error.code != std.fs::error_code::already_exists || len(failure.data) != 1) {
                    return 24;
                }
                drop failure;
                break;
        }
        std.fs::directory_iter iterator = await std.fs::iterate(&directory, o::none);
        try {
            std.fs::directory_next_result unexpected = await std.fs::next(move iterator, o::none);
            drop unexpected;
            return 20;
        } catch (std.async::start_error error) {
            error as void;
        }
        std.fs::directory_next_result next = await std.fs::next(move iterator, o::none);
        if (is_entry(move next) == false) {
            return 8;
        }
        std.fs::directory_iter second_iterator = await std.fs::iterate(&directory, o::none);
        std.fs::directory_next_result next_again = await std.fs::next(move second_iterator, o::none);
        switch (move next_again) {
            case variant std.fs::directory_next_result::entry(move payload):
                bool regular = payload.entry.kind == std.fs::file_kind::regular;
                drop payload;
                if (regular == false) { return 9; }
                break;
            case variant std.fs::directory_next_result::end:
                return 14;
            case variant std.fs::directory_next_result::failed(move failure):
                drop failure;
                return 15;
        }
        std.fs::path renamed = std.fs::path_from_utf8("renamed");
        i32 finally_count = 0;
        try {
            std.fs::metadata unexpected = await std.fs::metadata_beneath(&directory, &renamed, o::none);
            unexpected as void;
            return 25;
        } catch (std.fs::fs_error error) {
            if (error.code != std.fs::error_code::not_found) { return 26; }
        } finally {
            finally_count += 1;
        }
        if (finally_count != 1) { return 27; }
        await std.fs::rename_beneath(&directory, &file_name, &directory, &renamed, o::none);
        await std.fs::remove_file_beneath(&directory, &renamed, o::none);
        await std.fs::remove_file_beneath(&directory, &atomic_name, o::none);
        std.fs::directory_iter empty_iterator = await std.fs::iterate(&directory, o::none);
        std.fs::directory_next_result terminal = await std.fs::next(move empty_iterator, o::none);
        switch (move terminal) {
            case variant std.fs::directory_next_result::entry(move unexpected):
                drop unexpected;
                return 28;
            case variant std.fs::directory_next_result::end:
                break;
            case variant std.fs::directory_next_result::failed(move failure):
                drop failure;
                return 29;
        }
        try {
            await std.fs::close_directory(move directory, o::none);
            return 21;
        } catch (std.async::start_error error) {
            error as void;
        }
        await std.fs::close_directory(move directory, o::none);
        await std.fs::remove_directory_beneath(&root, &sub, o::none);
        await std.fs::close_directory(move root, o::none);
        return 0;
    } catch (std.fs::path_error error) {
        error as void;
        return 10;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 11;
    } catch (std.async::start_error error) {
        error as void;
        return 12;
    } catch (std.fs::fs_error error) {
        error as void;
        return 13;
    }
}
