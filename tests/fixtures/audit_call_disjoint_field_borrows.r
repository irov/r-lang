module audit.call_disjoint_field_borrows;

struct Pair {
    i32 left;
    i32 right;
};

i32 observe(i32* left, const i32* right) {
    *left = 19;
    return *right;
}

i32 main() {
    Pair pair = Pair { .left = 17, .right = 23 };
    i32 result = observe(&pair.left, &pair.right);
    result as void;
    i32 selected = pair.left == 19 && result == 23 ? 0 : 1;
    return selected;
}
