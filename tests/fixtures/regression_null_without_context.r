module regression.null_without_context;

i32 main() {
    bool invalid = null;
    i32 selected = invalid == true ? 0 : 1;
    return selected;
}
