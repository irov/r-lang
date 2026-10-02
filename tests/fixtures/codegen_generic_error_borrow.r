module test.codegen.generic_error_borrow;

@generic<T>
error Failure {
    T value;
};

@generic<E: error>
protected void propagate(E failure) throws E {
    throw move failure;
}

@generic<T>
protected void reject(T value) throws Failure<T> {
    Failure<T> failure = Failure<T> { .value = move value };
    propagate(move failure);
}

i32 main() {
    own i32* owner = new i32(41);
    const i32* pointer = &*owner;
    try {
        reject(pointer);
        return 1;
    } catch (Failure<const i32*> failure) {
        if (*(failure.value) != 41) {
            return 2;
        }
    }
    if (*owner != 41) {
        return 3;
    }
    return 0;
}
