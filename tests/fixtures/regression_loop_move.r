module test.regression.loop_move;

i32 main() {
    own i32* owner = new i32(9);
    i32 iteration = 0;
    while (iteration < 2) {
        if (*owner != 9) {
            return 1;
        }
        own i32* consumed = move owner;
        iteration += 1;
    }
    return 0;
}
