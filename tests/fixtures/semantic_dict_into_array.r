module test.semantic.dict_into_array;

/* R-EXPR-0030: a dict expression requires a dict<K, V> destination type. */
i32 main() {
    try {
        array<i32> values = {1: 2};
        return 0;
    } catch (std.dict::insert_error<i32, i32> failure) {
        failure as void;
        return 1;
    }
}
