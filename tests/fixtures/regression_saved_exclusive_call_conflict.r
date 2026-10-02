module regression.saved_exclusive_call_conflict;

void write_value(i32* target) {
    *target = 29;
}

i32 main() {
    i32 value = 17;
    i32* exclusive = &value;
    const i32* shared = &*exclusive;
    write_value(exclusive);
    return *shared;
}
