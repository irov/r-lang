module test.codegen.generic_conditional_borrow_same_origin;

@generic<T: copy>
T choose_one(bool first, T value) {
    T chosen = (first == true) ? value : value;
    return chosen;
}

@generic<T: copy>
T choose_two(bool first, T left, T right) {
    T chosen = (first == true) ? left : right;
    return chosen;
}

@generic<T: copy>
T choose_sequential(T left, T right) {
    T selected = right;
    T selected_2 = left;
    return selected_2;
}

@generic<T: copy>
T choose_constant(T left, T right) {
    T selected = right;
    T final_value = (true) ? left : selected;
    return final_value;
}

@generic<T: copy>
T forward(T value) {
    return value;
}

@generic<T: copy>
T forward_after_branch(bool repeat, T value) {
    T selected = value;
    if (repeat == true) {
        selected = value;
    }
    T result = forward(selected);
    return result;
}

const i32* select(bool first, const i32* value) {
    const i32* selected = choose_one(first, value);
    return selected;
}

const i32* select_stable(const i32* stable) {
    i32 local = 31;
    const i32* ephemeral = &local;
    const i32* sequential = choose_sequential(stable, ephemeral);
    const i32* constant = choose_constant(sequential, ephemeral);
    const i32* forwarded = forward_after_branch(true, constant);
    return forwarded;
}

i32 main() {
    i32 integer = choose_two(false, 11, 29);
    i32 value = 23;
    const i32* pointer = &value;
    const i32* selected = select(false, pointer);
    const i32* stable = select_stable(pointer);
    return (*selected - 23) + (*stable - 23) + (integer - 29);
}
