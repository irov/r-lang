module semantic.io_read_result_missing_variant;

void inspect(std.io::read_result result) {
    switch (move result) {
        case variant std.io::read_result::read(move payload):
            drop payload;
            break;
        case variant std.io::read_result::end(move buffer):
            drop buffer;
            break;
    }
}
