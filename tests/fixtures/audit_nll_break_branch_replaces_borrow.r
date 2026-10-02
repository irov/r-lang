module audit.nll_break_branch_replaces_borrow;

i32 run(bool stop) {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    while (true) {
        source += 1;
        if (stop == true) {
            break;
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
