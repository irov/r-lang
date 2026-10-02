module audit.compound_rhs_overlapping_write;

protected i32 overwrite(i32* value) {
    *value = 40;
    return 2;
}

i32 main() {
    i32 value = 1;
    value += overwrite(&value);
    i32 selected = value == 3 ? 0 : 1;
    return selected;
}
