module test.codegen.async_io_read;

async i32 main() {
    try {
        std.io::input stream = std.io::stdin();
        try {
            array<u8> buffer = std.array::with_capacity::<u8>(1);
            u8 placeholder = 0;
            std.array::push(&buffer, placeholder);
            try {
                task<std.io::read_result> operation =
                    std.io::read(&stream, move buffer, o::none);
                std.io::read_result completed = await move operation;
                drop completed;
                return 0;
            } catch (std.async::start_error error) {
                usize retained = len(buffer);
                error as void;
                if (retained == 1) {
                    throw TestAssertionFailed {.code = 3};
                }
                throw TestAssertionFailed {.code = 4};
            }
        } catch (std.array::push_error<u8> error) {
            error as void;
            throw TestAssertionFailed {.code = 2};
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw TestAssertionFailed {.code = 1};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
