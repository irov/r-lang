module test.regression.generic_error_variant_borrow_owner_lifetime;

@generic<T>
error Failure {
    Plain,
    Borrowed(T),
};

@generic<T>
protected void fail(T value) throws Failure<T> {
    throw Failure<T>::Borrowed(move value);
}

i32 main() {
    own i32* owner = new i32(47);
    const i32* pointer = &*owner;
    try {
        fail(pointer);
        return 1;
    } catch (Failure<const i32*> failure) {
        switch (move failure) {
            case variant Failure<const i32*>::Plain:
                return 2;
            case variant Failure<const i32*>::Borrowed(value):
                drop owner;
                return **value - 47;
        }
    }
}
