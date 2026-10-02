module test.codegen.scoped_file;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { usize value; };

/* R-SLIB-FS-0013: scoped file writes and reads over a loaned buffer; the path is the first
   program argument and is created or truncated by the program. */
async i32 main(const str[] arguments) {
    try {
        if (len(arguments) != 2usize) { throw TestAssertionFailed {.code = 10}; }
        try {
            std.fs::path path = std.fs::path_from_utf8(arguments[1]);
            std.fs::open_file_options writing = std.fs::open_file_options { .access = std.fs::access::write,
                .create = std.fs::create_mode::open_or_create, .truncate = true, .append = false,
                .follow_final_symlink = false };
            std.fs::file file = await path.open_file(writing, o::none);
            std.string::string text = std.string::from_str("scoped file contents");
            bytes payload = (move text).into_bytes();
            TestStorage1 storage_written = {.value = 0usize};
            task_scope(1) writer {
                const u8[] first = payload.as_slice();
                storage_written.value = await file.write_from(first, o::none);
                if (storage_written.value == 0usize) { throw TestAssertionFailed {.code = 11}; }
                const u8[] rest = payload.as_slice();
                await std.fs::write_all_from(&file, rest[storage_written.value..len(rest)], o::none);
            }
            await file.flush(o::none);
            await (move file).close(o::none);
            std.fs::open_file_options reading = std.fs::open_file_options { .access = std.fs::access::read,
                .create = std.fs::create_mode::existing, .truncate = false, .append = false,
                .follow_final_symlink = false };
            std.fs::file reader = await path.open_file(reading, o::none);
            bytes buffer = std.alloc::bytes(64usize, 0u8);
            usize total = 0usize;
            task_scope(1) loader {
                while (true) {
                    u8[] window = buffer.as_slice_mut();
                    usize count = await std.fs::read_into(&reader, window[total..len(window)], o::none);
                    if (count == 0usize) { break; }
                    total += count;
                }
            }
            await (move reader).close(o::none);
            if (total != len(payload)) { throw TestAssertionFailed {.code = 12}; }
            const u8[] got = buffer.as_slice();
            const u8[] expected = payload.as_slice();
            if (std.bytes::equal(got[0usize..total], expected) == false) { throw TestAssertionFailed {.code = 13}; }
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
