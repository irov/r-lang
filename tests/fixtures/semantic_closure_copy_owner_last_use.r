module test.semantic.closure_copy_owner_last_use;

i32 main() {
    own i32* boxed = new i32(7);
    fn i32 read() { return *boxed; }
    auto copied = read;
    i32 value = copied();
    drop boxed;
    i32 selected = value == 7 ? 0 : 1;
    return selected;
}
