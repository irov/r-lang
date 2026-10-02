module test.semantic.membership_operand;

/* R-EXPR-0029: the operand tested against a dict names a place. */
i32 main() {
    try {
        dict<i32, i32> table = std.dict::create::<i32, i32>();
        if (7 in table) { return 1; }
    } catch (std.dict::insert_error<i32, i32> failure) {
        failure as void;
        return 2;
    }
    return 0;
}
