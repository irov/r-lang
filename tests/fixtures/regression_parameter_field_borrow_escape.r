module regression.parameter_field_borrow_escape;

struct Cell {
    i32 value;
};

const i32* choose(bool first, const Cell* left, const Cell* right) {
    if (first == true) {
        right = left;
    }
    const i32* result = &(right->value);
    return result;
}

const i32* leak_local(const Cell* stable) {
    Cell local = {.value = 29};
    const Cell* ephemeral = &local;
    const i32* selected = choose(true, ephemeral, stable);
    return selected;
}

i32 main() {
    Cell stable_value = {.value = 11};
    const Cell* stable = &stable_value;
    const i32* leaked = leak_local(stable);
    return *leaked - 29;
}
