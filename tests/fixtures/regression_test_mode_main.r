module test.regression.test_mode_main;
import std.test;

// R-FUNC-0025 (M24): in test mode the entry module declares no main, because the test entry
// takes its place.

@test
void runs() throws std.test::failure, std.alloc::alloc_error {
    std.test::check(true, "runs");
}

i32 main() {
    return 0;
}
