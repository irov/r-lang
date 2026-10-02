module regression.shared_reborrow_ends_before_mutation;

void write_value(i32* target) {
    *target = 29;
}

i32 main() {
    i32 value = 17;
    i32* exclusive = &value;
    const i32* shared = &*exclusive;
    i32 first = *shared;
    write_value(exclusive);
    return first + value - 46;
}
