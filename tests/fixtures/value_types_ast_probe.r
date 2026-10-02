module test.value_types.ast_probe;

error issue {
    i32 code;
};

i32 explicit_result(i32 value) throws issue {
    i32 first = value;
    o<i32> absent = o::none;
    o<i32> present = o::some(value);
    if (value == 0) {
        throw { .code = value };
    }
    absent as void;
    present as void;
    return first;
}
