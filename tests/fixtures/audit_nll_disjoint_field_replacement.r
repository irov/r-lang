module audit.nll_disjoint_field_replacement;

struct View {
    const i32* left;
    const i32* right;
};

i32 main() {
    i32 source = 7;
    i32 other = 9;
    i32 replacement = 11;
    View view = View {
        .left = &source,
        .right = &other,
    };
    source += 1;
    view.left = &replacement;
    View copy = view;
    return *copy.left + *copy.right - 20;
}
