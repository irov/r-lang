module test.semantic.comprehension_element_mismatch;

/* R-EXPR-0030: every produced element has the element type of the destination. */
i32 main() {
    try {
        array<i32> flags = [x > 1 for (i32 x in 0..3)];
        return 0;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 1;
    }
}
