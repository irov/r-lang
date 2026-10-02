module semantic.io_write_result_missing_variant;

void inspect(std.io::write_result result) {
    switch (move result) {
        case variant std.io::write_result::written(move payload):
            drop payload;
            break;
    }
}
