module test.audit.mixed_error_nonborrow_variant;

error MixedError {
    plain,
    borrowed(const i32*),
};

protected void fail() throws MixedError {
    throw MixedError::plain;
}

i32 main() {
    try {
        fail();
        return 1;
    } catch (MixedError failure) {
        failure as void;
        return 0;
    }
}
