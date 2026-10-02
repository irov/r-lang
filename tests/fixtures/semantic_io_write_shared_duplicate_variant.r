module semantic.io_write_shared_duplicate_variant;

void inspect(std.io::shared_write_result result) {
    switch (move result) {
        case variant std.io::shared_write_result::written(move first):
            drop first;
            break;
        case variant std.io::shared_write_result::written(move second):
            drop second;
            break;
        case variant std.io::shared_write_result::failed(move failure):
            drop failure;
            break;
    }
}
