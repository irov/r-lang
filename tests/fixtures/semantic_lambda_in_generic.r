module test.semantic.lambda_in_generic;

/* R-FUNC-0015: a generic closure body is checked before instantiation. */
@generic<T: copy>
T identity(T value) {
    fn T same(T inner) { return inner; }
    T result = same(value);
    return result;
}

i32 main() {
    i32 value = identity(3);
    return value;
}
