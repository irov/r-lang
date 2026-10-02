module audit.nll_break_target_uses_old_borrow;

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
    return *alias;
}

i32 main() {
    return 0;
}
