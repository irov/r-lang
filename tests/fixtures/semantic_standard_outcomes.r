module semantic.standard_outcomes;

i32 inspect_io(std.io::write_all_result result) {
    switch (move result) {
        case variant std.io::write_all_result::written(move buffer):
            usize count = len(buffer);
            drop buffer;
            return count as i32;
        case variant std.io::write_all_result::failed(move failure):
            std.io::io_error error = failure.error;
            usize written = failure.written;
            usize count = len(failure.buffer);
            error as void;
            count as void;
            drop failure;
            return written as i32;
    }
}

i32 inspect_fs(std.fs::write_file_result result) {
    switch (move result) {
        case variant std.fs::write_file_result::committed(move data):
            usize count = len(data);
            drop data;
            return count as i32;
        case variant std.fs::write_file_result::failed(move failure):
            bool exists = failure.error.code == std.fs::error_code::already_exists;
            usize count = len(failure.data);
            count as void;
            drop failure;
            if (exists == true) {
                return 4;
            }
            return 1;
    }
}
