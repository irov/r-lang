module test.codegen.multidimensional_array;

struct Counters {
    u16[16] values;
};

i32 main() {
    i32[2][3] values = {
        { 1, 2, 3 },
        { 4, 5, 6 },
    };
    Counters counters = {};
    usize index = 3;
    while (index < 4) {
        if (index == 3) {
            counters.values[index] += 1;
        }
        index += 1;
    }
    if (counters.values[3] != 1) {
        return 1;
    }
    return values[1usize][2usize] - 6;
}
