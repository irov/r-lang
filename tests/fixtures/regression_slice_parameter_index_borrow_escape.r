module regression.slice_parameter_index_borrow_escape;

const i32* choose(bool first, const i32[] left, const i32[] right) {
    if (first == true) {
        right = left;
    }
    const i32* result = &(right[0]);
    return result;
}

const i32* leak_local(const i32[] stable) {
    i32[1] local = {29};
    const i32[] ephemeral = &local;
    const i32* selected = choose(true, ephemeral, stable);
    return selected;
}

i32 main() {
    i32[1] stable_values = {11};
    const i32[] stable = &stable_values;
    const i32* leaked = leak_local(stable);
    return *leaked - 29;
}
