module test.aggregate;

enum color {
    red,
    green,
};

struct pair {
    i32 left;
    u32 right;
    color shade;
};

i32 main() {
    pair value = {
        .right = 9,
        .shade = color::green,
        .left = 3,
    };
    value.left = value.left + 1;
    if (value.shade == color::green) {
        if (value.left == 4) {
            return 0;
        }
        return 1;
    }
    return 1;
}
