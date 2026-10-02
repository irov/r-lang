module regression.move_reinitialization;

i32 main() {
    own i32* owner = new i32(17);
    own i32* consumed = move owner;
    drop consumed;
    owner = new i32(23);
    if (*owner != 23) {
        return 1;
    }
    return 0;
}
