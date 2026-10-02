module regression.multi_origin_mutation;

const i32* choose(bool choose_first, const i32* first, const i32* second) {
    const i32* chosen = (choose_first == true) ? first : second;
    return chosen;
}

i32 main() {
    i32 first = 1;
    i32 second = 2;
    const i32* selected = choose(true, &first, &second);
    first = 3;
    return *selected;
}
