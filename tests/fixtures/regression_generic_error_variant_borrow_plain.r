module test.regression.generic_error_variant_borrow_plain;

@generic<T>
error Mixed {
    Plain,
    Borrowed(T),
};

@generic<T>
protected void fail(bool borrow, T value) throws Mixed<T> {
    throw (borrow == true) Mixed<T>::Borrowed(move value) else Mixed<T>::Plain;
}

i32 main() {
    own i32* owner = new i32(47);
    const i32* pointer = &*owner;
    try {
        fail(false, pointer);
        return 1;
    } catch (Mixed<const i32*> failure) {
        switch (move failure) {
            case variant Mixed<const i32*>::Plain:
                drop owner;
                return 0;
            case variant Mixed<const i32*>::Borrowed(value):
                return **value - 47;
        }
    }
}
