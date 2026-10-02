module audit.nll_assignment_rhs_reads_old_borrow;

i32 main() {
    i32 source = 7;
    const i32* alias = &source;
    source += 1;
    alias = alias;
    return *alias;
}
