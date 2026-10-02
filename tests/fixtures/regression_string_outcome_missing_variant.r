module test.regression.string_outcome_missing_variant;
i32 inspect(std.string::from_bytes_result result) {
    switch (move result) {
    case variant std.string::from_bytes_result::valid(move text): return 0;
    }
}
