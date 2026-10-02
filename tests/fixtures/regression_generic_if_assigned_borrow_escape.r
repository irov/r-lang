module regression.generic_if_assigned_borrow_escape;

@generic<T: copy>
T choose(bool first, T left, T right) {
    T selected = right;
    if (first == true) {
        selected = left;
    }
    return selected;
}

const i32* leak_local(const i32* stable) {
    i32 local = 29;
    const i32* ephemeral = &local;
    const i32* selected = choose(false, stable, ephemeral);
    return selected;
}

i32 main() {
    i32 stable_value = 11;
    const i32* stable = &stable_value;
    const i32* leaked = leak_local(stable);
    return *leaked - 29;
}
