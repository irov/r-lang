module regression.option_payload_borrow_parameter_return;

o<const i32*> wrap(const i32* value) {
    return o::some(value);
}

i32 main() {
    i32 source = 29;
    const i32* borrowed = &source;
    o<const i32*> wrapped = wrap(borrowed);
    switch (wrapped) {
    case variant o::some(move value):
        return *value - 29;
    case variant o::none:
        return 1;
    }
}
