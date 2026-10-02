module test.codegen.async_standard_outcomes;

async i32 inspect_io(std.io::write_all_result result) {
    switch (move result) {
        case variant std.io::write_all_result::written(move buffer):
            usize count = len(buffer);
            count as void;
            drop buffer;
            return 0;
        case variant std.io::write_all_result::failed(move failure):
            std.io::io_error error = failure.error;
            usize written = failure.written;
            error as void;
            written as void;
            drop failure;
            return 1;
    }
}

async i32 main() {
    return 0;
}
