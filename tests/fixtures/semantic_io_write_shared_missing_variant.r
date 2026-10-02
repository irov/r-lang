module semantic.io_write_shared_missing_variant;

void inspect(std.io::shared_write_result result) {
    switch (move result) {
        case variant std.io::shared_write_result::written(move buffer):
            drop buffer;
            break;
    }
}
