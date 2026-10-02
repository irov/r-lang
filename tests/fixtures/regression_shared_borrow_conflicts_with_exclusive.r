module regression.shared_borrow_conflicts_with_exclusive;

i32 main() {
    i32 value = 17;
    i32* exclusive = &value;
    const i32* shared = &value;
    *exclusive = 29;
    return *shared;
}
