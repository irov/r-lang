module test.semantic.spread_non_variadic;

/* R-FUNC-0018: a spread argument forwards a slice only to a variadic parameter. */
i32 first(const i32[] values) {
    return values[0];
}

i32 main() {
    i32[2] pair = {1, 2};
    const i32[] view = &pair;
    i32 result = first(...view);
    return result;
}
