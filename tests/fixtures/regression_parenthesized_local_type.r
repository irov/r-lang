module regression.parenthesized_local_type;

i32 main() {
    (i32) value = 1;
    ((i32)) nested = 2;
    (value + nested) as void;
    (i32)* borrowed = &value;
    return *borrowed + nested - 3;
}
