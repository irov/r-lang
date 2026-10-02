module codegen.discard;

struct Point {
    i32 x;
    i32 y;
};

i32 bump(i32* value) {
    *value += 1;
    return *value;
}

i32 main() {
    i32 value = 4;
    bump(&value) as void;
    Point point = {
        .x = value,
        .y = 9,
    };
    point as void;
    if (value == 5) {
        return 0;
    } else {
        return 1;
    }
}
