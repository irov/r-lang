module semantic.standard_outcomes_wrong_variant;

void inspect(std.fs::write_file_result result) {
    switch (move result) {
        case variant std.fs::write_file_result::written:
            break;
        case variant std.fs::write_file_result::failed:
            break;
    }
}
