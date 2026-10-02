module regression.generic_error_call_automatic_borrow_escape;

@generic<T>
error Failure {
    const T* pointer;
};

@generic<E: error>
void propagate(E failure) throws E {
    throw move failure;
}

@generic<T>
void fail(T value) throws Failure<T> {
    Failure<T> failure = Failure<T> { .pointer = &value };
    propagate(move failure);
}

void instantiate() throws Failure<i32> {
    fail(7);
}

i32 main() {
    return 0;
}
