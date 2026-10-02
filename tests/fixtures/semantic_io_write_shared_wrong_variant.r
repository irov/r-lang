module semantic.io_write_shared_wrong_variant;

void inspect(std.io::shared_write_result result) {
    switch (move result) {
        case variant std.io::shared_write_result::read:
            break;
        case variant std.io::shared_write_result::failed(move failure):
            drop failure;
            break;
    }
}
