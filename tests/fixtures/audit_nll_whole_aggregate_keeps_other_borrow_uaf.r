module audit.nll_whole_aggregate_keeps_other_borrow_uaf;

struct View {
    const i32* left;
    const i32* right;
};

i32 main() {
    i32 source = 7;
    i32 replacement = 11;
    View view = View {
        .left = &source,
        .right = &source,
    };
    source += 1;
    view.left = &replacement;
    View copy = view;
    return *copy.right;
}
