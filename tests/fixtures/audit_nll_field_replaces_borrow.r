module audit.nll_field_replaces_borrow;

struct View {
    const i32* alias;
};

i32 main() {
    i32 source = 7;
    i32 replacement = 11;
    View view = {
        .alias = &source,
    };
    source += 1;
    view.alias = &replacement;
    return *view.alias - 11;
}
