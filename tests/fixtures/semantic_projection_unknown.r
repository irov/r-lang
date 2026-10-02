module test.semantic.projection_unknown;

/* R-TYPE-0045: P::Name requires a trait constraint of P that declares Name. */
@generic<T: copy>
T::Item first(const T* value) {
    return *value;
}

i32 main() {
    return 0;
}
