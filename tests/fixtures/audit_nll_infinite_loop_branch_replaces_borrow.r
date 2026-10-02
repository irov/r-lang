module audit.nll_infinite_loop_branch_replaces_borrow;

i32 select(bool stop) {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    source += 1;
    if (stop == true) {
        while (true) {
        }
    } else {
        alias = &replacement;
    }
    return *alias - 11;
}

i32 main() {
    i32 result = select(false);
    return result;
}
