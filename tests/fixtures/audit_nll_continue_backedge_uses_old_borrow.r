module audit.nll_continue_backedge_uses_old_borrow;

i32 run(bool skip) {
    i32 source = 7;
    const i32* alias = &source;
    while (true) {
        if (skip == false) {
            return *alias;
        }
        source += 1;
        skip = false;
        continue;
    }
}

i32 main() {
    return 0;
}
