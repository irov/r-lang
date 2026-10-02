module test.codegen.async_io_write_shared;

async i32 main() {
    try {
        std.io::output stream = std.io::stdout();
        try {
            array<u8> buffer = std.array::with_capacity::<u8>(1);
            u8 payload_byte = 83;
            std.array::push(&buffer, payload_byte);
            arc (array<u8>) shared = new arc (array<u8>)(move buffer);
            try {
                task<std.io::shared_write_result> operation =
                            std.io::write_shared(&stream, move shared, 0, 1, o::none);
                std.io::shared_write_result completed = await move operation;
                switch (move completed) {
                    case variant std.io::shared_write_result::written(move returned):
                        drop returned;
                        return 0;
                    case variant std.io::shared_write_result::failed(move failure):
                        std.io::io_error error = failure.error;
                        usize written = failure.written;
                        error as void;
                        written as void;
                        drop failure;
                        throw TestAssertionFailed {.code = 10};
                        }
            } catch (std.async::start_error error) {
                error as void;
                drop shared;
                throw TestAssertionFailed {.code = 11};
            }
        } catch (std.array::push_error<u8> error) {
            error as void;
            throw TestAssertionFailed {.code = 12};
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw TestAssertionFailed {.code = 13};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
