module test.codegen.async_io_write_all;

async i32 main() {
    try {
        std.io::output stream = std.io::stderr();
        try {
            array<u8> buffer = std.array::with_capacity::<u8>(0);
            try {
                task<std.io::write_all_result> operation =
                    std.io::write_all(&stream, move buffer, o::none);
                std.io::write_all_result completed = await move operation;
                switch (move completed) {
                    case variant std.io::write_all_result::written(move returned):
                        usize count = len(returned);
                        drop returned;
                        if (count == 0) {
                            return 0;
                        }
                        throw TestAssertionFailed {.code = 4};
                    case variant std.io::write_all_result::failed(move failure):
                        drop failure;
                        throw TestAssertionFailed {.code = 3};
                }
            } catch (std.async::start_error error) {
                usize retained = len(buffer);
                error as void;
                if (retained == 0) {
                    throw TestAssertionFailed {.code = 2};
                }
                throw TestAssertionFailed {.code = 5};
            }
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw TestAssertionFailed {.code = 1};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
