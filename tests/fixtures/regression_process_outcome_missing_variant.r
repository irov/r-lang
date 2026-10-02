module regression.process_outcome_missing_variant;

void consume(std.process::wait_result result) {
    switch (move result) {
    case variant std.process::wait_result::exited(move status): return;
    }
}

i32 main() { return 0; }
