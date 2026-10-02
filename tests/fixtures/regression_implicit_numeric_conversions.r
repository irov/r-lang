module regression.implicit_numeric_conversions;

i64 widen_return(i32 value) {
    return value;
}

void take_promoted(i32 value) {
}

i32 main() {
    i8 small = 7;
    i32 promoted = small;
    i64 wide = promoted;
    i64 wide_2 = promoted;

    f32 narrow_float = 1.5f32;
    f64 wide_float = narrow_float;

    take_promoted(small);
    i64 returned = widen_return(promoted);
    returned as void;
    i32 selected = wide_2 == 7i64 && wide_float == 1.5f64 && returned == 7i64 ? 0 : 1;
    return selected;
}
