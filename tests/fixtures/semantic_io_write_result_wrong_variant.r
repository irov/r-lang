module semantic.io_write_result_wrong_variant;

void inspect(std.io::write_result result) {
    switch (move result) {
        case variant std.io::write_result::read:
            break;
        case variant std.io::write_result::failed(move failure):
            drop failure;
            break;
    }
}
