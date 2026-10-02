module regression.generic_error_variant_automatic_borrow_escape;

@generic<T>
error Failure {
    Invalid(const T*),
};

@generic<T>
void fail(T value) throws Failure<T> {
    throw Failure<T>::Invalid(&value);
}

void instantiate() throws Failure<i32> {
    fail(7);
}

i32 main() {
    return 0;
}
