module test.audit.generic_error_borrow_argument;

@generic<T>
error Failure {
    T value;
};

@generic<T>
error VariantFailure {
    Rejected(T),
};

@generic<T>
protected void reject(T value) throws Failure<T> {
    Failure<T> failure = Failure<T> { .value = move value };
    throw move failure;
}

protected void forward(const i32* value) throws Failure<const i32*> {
    reject(value);
}

protected void forward_slice(const i32[] value) throws Failure<const i32[]> {
    reject(value);
}

protected void forward_string(str value) throws Failure<str> {
    reject(value);
}

@generic<T>
protected void reject_variant(T value) throws VariantFailure<T> {
    throw VariantFailure<T>::Rejected(move value);
}

protected void forward_variant(const i32* value) throws VariantFailure<const i32*> {
    reject_variant(value);
}
