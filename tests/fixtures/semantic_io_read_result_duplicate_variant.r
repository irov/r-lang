module semantic.io_read_result_duplicate_variant;

void inspect(std.io::read_result result) {
    switch (move result) {
        case variant std.io::read_result::read(move first):
            drop first;
            break;
        case variant std.io::read_result::read(move second):
            drop second;
            break;
        case variant std.io::read_result::end(move buffer):
            drop buffer;
            break;
        case variant std.io::read_result::failed(move failure):
            drop failure;
            break;
    }
}
