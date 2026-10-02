module semantic.io_write_result_duplicate_variant;

void inspect(std.io::write_result result) {
    switch (move result) {
        case variant std.io::write_result::written(move first):
            drop first;
            break;
        case variant std.io::write_result::written(move second):
            drop second;
            break;
        case variant std.io::write_result::failed(move failure):
            drop failure;
            break;
    }
}
