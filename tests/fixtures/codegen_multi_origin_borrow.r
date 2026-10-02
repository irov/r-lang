module test.codegen.multi_origin_borrow;

const i32* choose_conditional(
    bool choose_first,
    const i32* first,
    const i32* second) {
    const i32* selected = (choose_first == true) ? first : second;
    return selected;
}

const i32* choose_returns(
    bool choose_first,
    const i32* first,
    const i32* second) {
    if (choose_first == true) {
        return first;
    }
    return second;
}

const i32* choose_subset(
    bool choose_first,
    const i32* first,
    const i32* second,
    const i32* unused) {
    unused as void;
    const i32* selected = (choose_first == true) ? first : second;
    return selected;
}

i32 main() {
    i32 first = 11;
    i32 second = 29;
    i32 unused = 41;
    const i32* selected_first = choose_conditional(true, &first, &second);
    const i32* selected_second = choose_returns(false, &first, &second);
    const i32* selected_subset = choose_subset(true, &first, &second, &unused);
    i32 unused_2 = 43;
    return (*selected_first - 11) + (*selected_second - 29) + (*selected_subset - 11) +
           (unused_2 - 43);
}
