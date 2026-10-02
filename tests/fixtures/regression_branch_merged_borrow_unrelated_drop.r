module regression.branch_merged_borrow_unrelated_drop;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


i32 main() {
    try {
        try {
            std.json::value left = std.json::parse("\"left-owned-text\"");
            std.json::value right = std.json::parse("\"right-owned-text\"");
            test_observe(&right);
            std.json::value unrelated = std.json::parse("null");
            bool choose_right = false;
            str selected = std.json::text(&left);
            selected as void; // The branch may choose a different view.
            if (choose_right == true) {
                selected = std.json::text(&right);
            }
            drop unrelated;
            std.string::string copy = std.string::from_str(selected);
            drop copy;
            return 0;
        } catch (std.json::error failure) {
            drop failure;
            throw TestAssertionFailed {.code = 2};
        } catch (std.alloc::alloc_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 1};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
