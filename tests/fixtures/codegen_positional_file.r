module test.codegen.positional_file;

/* R-SLIB-FS-0014..0015: sync at every level, positional reads and writes in owned and scoped
   forms, a hole past the end, the shared position kept, an offset beyond i64 and a read-only
   sync; the path is the first program argument and is created or truncated by the program. */

/* Whether data equals expected, where '.' stands for a zero byte. */
protected bool same(const u8[] data, str expected) {
    const u8[] want = expected;
    if (len(data) != len(want)) { return false; }
    for (usize index = 0usize; index < len(data); index += 1usize) {
        u8 wanted = want[index];
        if (wanted == 46u8) { wanted = 0u8; }
        if (data[index] != wanted) { return false; }
    }
    return true;
}

/* Whether a read from offset 0 returned exactly expected. */
protected bool read_is(std.io::read_result read, str expected) throws std.io::io_error {
    switch (move read) {
    case variant std.io::read_result::read(move part):
        const u8[] all = std.array::as_slice(&part.buffer);
        return same(all[0usize..part.count], expected);
    case variant std.io::read_result::end(move returned): drop returned;
    case variant std.io::read_result::failed(move failure): throw failure.error;
    }
    return false;
}

async i32 main(const str[] arguments) {
    try {
        if (len(arguments) != 2usize) { throw TestAssertionFailed {.code = 10}; }
        try {
            std.fs::path path = std.fs::path_from_utf8(arguments[1]);
            std.fs::open_file_options writing = std.fs::open_file_options { .access = std.fs::access::read_write,
                .create = std.fs::create_mode::open_or_create, .truncate = true, .append = false,
                .follow_final_symlink = false };
            std.fs::file file = await path.open_file(writing, o::none);
            std.string::string digits = std.string::from_str("0123456789");
            bytes digit_bytes = (move digits).into_bytes();
            std.io::write_all_result first = await file.write_all(move digit_bytes, o::none);
            drop first;
            await std.fs::sync(&file, std.fs::sync_level::barrier, o::none);
            await file.sync(std.fs::sync_level::device, o::none);
            await file.sync(std.fs::sync_level::media, o::none);
            std.string::string tail = std.string::from_str("XY");
            bytes tail_bytes = (move tail).into_bytes();
            std.io::write_all_result second = await file.write_all_at(12u64, move tail_bytes, o::none);
            drop second;
            bytes whole = std.alloc::bytes(32usize, 0u8);
            std.io::read_result first_read = await std.fs::read_at(&file, 0u64, move whole, o::none);
            if (read_is(move first_read, "0123456789..XY") == false) { throw TestAssertionFailed {.code = 11}; }
            u64 position = await file.seek(std.fs::seek_origin::current, 0i64, o::none);
            if (position != 10u64) { throw TestAssertionFailed {.code = 12}; }
            bytes window = std.alloc::bytes(3usize, 0u8);
            std.string::string head = std.string::from_str("ab");
            usize count = 0usize;
            task_scope(1) io {
                count += await file.read_at_into(5u64, window.as_slice_mut(), o::none);
                await std.fs::write_all_at_from(&file, 0u64, head, o::none);
            }
            if (count != 3usize || same(window.as_slice(), "567") == false) { throw TestAssertionFailed {.code = 13}; }
            bytes again = std.alloc::bytes(32usize, 0u8);
            std.io::read_result second_read = await file.read_at(0u64, move again, o::none);
            if (read_is(move second_read, "ab23456789..XY") == false) { throw TestAssertionFailed {.code = 14}; }
            bytes beyond = std.alloc::bytes(1usize, 0u8);
            std.io::read_result refused = await file.read_at(9223372036854775808u64, move beyond, o::none);
            switch (move refused) {
            case variant std.io::read_result::failed(move failure):
                if (failure.error.code != std.io::error_code::invalid_operation) { throw TestAssertionFailed {.code = 15}; }
            case variant std.io::read_result::read(move part): throw TestAssertionFailed {.code = 16};
            case variant std.io::read_result::end(move returned): throw TestAssertionFailed {.code = 17};
            }
            await (move file).close(o::none);
            std.fs::open_file_options reading = std.fs::open_file_options { .access = std.fs::access::read,
                .create = std.fs::create_mode::existing, .truncate = false, .append = false,
                .follow_final_symlink = false };
            std.fs::file reader = await path.open_file(reading, o::none);
            bool read_only_refused = false;
            try {
                await reader.sync(std.fs::sync_level::barrier, o::none);
            } catch (std.fs::fs_error failure) {
                read_only_refused = failure.code == std.fs::error_code::invalid_operation;
            }
            await (move reader).close(o::none);
            if (read_only_refused == false) { throw TestAssertionFailed {.code = 18}; }
            return 0;
        } catch (std.fs::path_error failure) { throw TestAssertionFailed {.code = 65}; }
        catch (std.fs::fs_error failure) { throw TestAssertionFailed {.code = 66}; }
        catch (std.io::io_error failure) { throw TestAssertionFailed {.code = 74}; }
        catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 71}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 75}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
