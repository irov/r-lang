module test.codegen.async_io_read_result;

async i32 main() {
    std.io::input stream = std.io::stdin();
    try {
        array<u8> buffer = std.array::with_capacity::<u8>(1);
        u8 placeholder = 0;
        std.array::push(&buffer, placeholder);
        try {
            task<std.io::read_result> operation =
                std.io::read(&stream, move buffer, o::none);
            std.io::read_result completed = await move operation;
            switch (move completed) {
                case variant std.io::read_result::read(move payload):
                    usize count = payload.count;
                    usize length = len(payload.buffer);
                    drop payload;
                    if ((count == 1) && (length == 1)) {
                        return 0;
                    }
                    return 10;
                case variant std.io::read_result::end(move returned):
                    usize length = len(returned);
                    drop returned;
                    return 11;
                case variant std.io::read_result::failed(move failure):
                    std.io::io_error error = failure.error;
                    usize count = failure.count;
                    usize length = len(failure.buffer);
                    error as void;
                    count as void;
                    length as void;
                    drop failure;
                    return 12;
            }
        } catch (std.async::start_error error) {
            usize retained = len(buffer);
            error as void;
            drop buffer;
            if (retained == 1) {
                return 13;
            }
            return 14;
        }
    } catch (std.array::push_error<u8> error) {
        drop stream;
        error as void;
        return 15;
    } catch (std.alloc::alloc_error error) {
        drop stream;
        error as void;
        return 16;
    }
}
