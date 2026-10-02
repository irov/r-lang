module regression.generic_conditional_borrow_escape;

@generic<T: copy>
T choose(bool first, T left, T right) {
    T chosen = (first == true) ? left : right;
    return chosen;
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
