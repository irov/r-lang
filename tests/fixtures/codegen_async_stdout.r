module test.codegen.async_stdout;

async i32 main() {
    std.io::output stream = std.io::stdout();
    std.io::output selected = move stream;
    try {
        array<u8> buffer = std.array::with_capacity::<u8>(1);
        u8 byte = 82;
        std.array::push(&buffer, byte);
        try {
            task<std.io::write_all_result> operation =
                        std.io::write_all(&selected, move buffer, o::none);
            std.io::write_all_result completed = await move operation;
            switch (move completed) {
                case variant std.io::write_all_result::written(move returned):
                    usize count = len(returned);
                    drop returned;
                    if (count == 1) {
                        return 0;
                    }
                    return 7;
                case variant std.io::write_all_result::failed(move failure):
                    drop failure;
                    return 6;
            }
        } catch (std.async::start_error error) {
            usize retained = len(buffer);
            error as void;
            if (retained == 1) {
                return 5;
            }
            return 8;
        }
    } catch (std.array::push_error<u8> error) {
        error as void;
        return 4;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 3;
    }
}
