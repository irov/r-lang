module test.codegen.async_new_expression;

struct Item {
    i32 value;
};

async i32 main() {
    own i32* scalar = new i32(7);
    arc Item shared = new arc Item {
        .value = 11,
    };
    rc Item local = new rc Item {
        .value = 13,
    };
    return 0;
}
