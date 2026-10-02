module test.codegen.async_io_write_result;

async i32 main() {
    std.io::output stream = std.io::stdout();
    try {
        array<u8> buffer = std.array::with_capacity::<u8>(1);
        u8 payload_byte = 87;
        std.array::push(&buffer, payload_byte);
        try {
            task<std.io::write_result> operation =
                        std.io::write(&stream, move buffer, o::none);
            std.io::write_result completed = await move operation;
            switch (move completed) {
                case variant std.io::write_result::written(move payload):
                    usize count = payload.count;
                    usize length = len(payload.buffer);
                    drop payload;
                    if ((count == 1) && (length == 1)) {
                        return 0;
                    }
                    return 10;
                case variant std.io::write_result::failed(move failure):
                    std.io::io_error error = failure.error;
                    usize written = failure.written;
                    usize length = len(failure.buffer);
                    error as void;
                    written as void;
                    length as void;
                    drop failure;
                    return 11;
            }
        } catch (std.async::start_error error) {
            usize retained = len(buffer);
            error as void;
            drop buffer;
            if (retained == 1) {
                return 12;
            }
            return 13;
        }
    } catch (std.array::push_error<u8> error) {
        drop stream;
        error as void;
        return 14;
    } catch (std.alloc::alloc_error error) {
        drop stream;
        error as void;
        return 15;
    }
}
