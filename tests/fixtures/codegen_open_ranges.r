module test.codegen.open_ranges;

/* R-EXPR-0021: a range may omit either bound; the array length and zero stand in. */
protected u32 sum(const u32[] values) {
    u32 total = 0;
    usize index = 0;
    while (index < len(values)) {
        total += values[index];
        index += 1;
    }
    return total;
}

protected u32 tail_sum(const u32[] values, usize from) {
    const u32[] tail = values[from..];
    u32 total = sum(tail);
    return total;
}

i32 main() {
    try {
        u32[6] data = {};
        usize fill = 0;
        while (fill < 6) {
            data[fill] = (fill + 1) as u32;
            fill += 1;
        }
        const u32[] whole = data[..];
        const u32[] head = data[..3];
        const u32[] rest = data[2..];
        u32 whole_sum = sum(whole);
        u32 head_sum = sum(head);
        u32 rest_sum = sum(rest);
        u32 shifted = tail_sum(whole, 4);
        if (whole_sum != 21) { throw TestAssertionFailed {.code = 1}; }
        if (head_sum != 6) { throw TestAssertionFailed {.code = 2}; }
        if (rest_sum != 18) { throw TestAssertionFailed {.code = 3}; }
        if (shifted != 11) { throw TestAssertionFailed {.code = 4}; }
        const u32[] empty = rest[4..];
        if (len(empty) != 0) { throw TestAssertionFailed {.code = 5}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
