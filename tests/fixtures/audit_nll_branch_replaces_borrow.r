module audit.nll_branch_replaces_borrow;

i32 select(bool choose) {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    source += 1;
    if (choose == true) {
        alias = &replacement;
    } else {
        alias = &replacement;
    }
    return *alias - 11;
}

i32 main() {
    i32 first = select(true);
    i32 second = select(false);
    return first + second;
}
