module semantic.io_read_result_wrong_variant;

void inspect(std.io::read_result result) {
    switch (move result) {
        case variant std.io::read_result::written:
            break;
        case variant std.io::read_result::end(move buffer):
            drop buffer;
            break;
        case variant std.io::read_result::failed(move failure):
            drop failure;
            break;
    }
}
