module audit.nll_continue_branch_replaces_borrow;

i32 run(bool skip) {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    bool repeat = true;
    while (repeat == true) {
        repeat = false;
        source += 1;
        if (skip == true) {
            continue;
        } else {
            alias = &replacement;
        }
        return *alias - 11;
    }
    return 0;
}

i32 main() {
    i32 first = run(true);
    i32 second = run(false);
    return first + second;
}
