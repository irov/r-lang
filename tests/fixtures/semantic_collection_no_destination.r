module test.semantic.collection_no_destination;

/* R-EXPR-0030: an array expression requires an array<T> destination type. */
i32 main() {
    try {
        i32 value = [1, 2];
        return value;
    } catch (std.array::push_error<i32> failure) {
        failure as void;
        return 1;
    }
}
