module test.codegen.async_replacement_shapes;

async void checkpoint() { return; }

async i32 main() {
    try {
        try {
            bytes buffer = std.bytes::with_capacity(8usize);
            std.bytes::append(&buffer, "old");
            bytes previous = core::take(&buffer);
            bool preserved = std.bytes::equal(previous, "old");
            if (len(buffer) != 0usize || len(previous) != 3usize || preserved == false) { throw TestAssertionFailed {.code = 1}; }
            std.bytes::append(&buffer, "new");
            std.string::string current = std.string::from_str("before");
            std.string::string next = std.string::from_str("after");
            std.string::string old = core::replace(&current, move next);
            str old_text = old;
            str new_text = current;
            bool old_matches = std.bytes::equal(old_text, "before");
            bool new_matches = std.bytes::equal(new_text, "after");
            if (old_matches == false || new_matches == false) { throw TestAssertionFailed {.code = 2}; }
            (own i32*?)[2] slots = {new i32(42)};
            (own i32*?)[2] extracted = core::take(&slots);
            if (slots[0] != null || slots[1] != null || extracted[0] == null) { throw TestAssertionFailed {.code = 3}; }
            own i32*? selected = core::take(&extracted[0]);
            if (selected != null) { if (*selected != 42) { throw TestAssertionFailed {.code = 4}; } }
            else { throw TestAssertionFailed {.code = 5}; }
            await checkpoint();
            if (len(previous) != 3usize || len(buffer) != 3usize || extracted[0] != null) { throw TestAssertionFailed {.code = 6}; }
            return 0;
        } catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 7}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 8}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
