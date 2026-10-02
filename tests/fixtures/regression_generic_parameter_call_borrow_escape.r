module regression.generic_parameter_call_borrow_escape;

@generic<T: copy>
T identity(T value) {
    return value;
}

@generic<T: copy>
T choose(bool first, T left, T right) {
    if (first == true) {
        right = left;
    }
    T result = identity(right);
    return result;
}

const i32* leak_local(const i32* stable) {
    i32 local = 29;
    const i32* ephemeral = &local;
    const i32* selected = choose(true, ephemeral, stable);
    return selected;
}

i32 main() {
    i32 stable_value = 11;
    const i32* stable = &stable_value;
    const i32* leaked = leak_local(stable);
    return *leaked - 29;
}
