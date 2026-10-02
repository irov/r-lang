module semantic.standard_outcomes_missing_move;

// Without an outer move the switch borrows the outcome, so a payload cannot be moved out.
void inspect(std.io::write_all_result result) {
    switch (result) {
        case variant std.io::write_all_result::written(move buffer):
            buffer as void;
            break;
        case variant std.io::write_all_result::failed:
            break;
    }
}
