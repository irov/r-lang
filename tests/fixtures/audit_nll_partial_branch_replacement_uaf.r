module audit.nll_partial_branch_replacement_uaf;

i32 select(bool choose) {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    source += 1;
    if (choose == true) {
        alias = &replacement;
    }
    return *alias;
}

i32 main() {
    i32 result = select(false);
    return result;
}
