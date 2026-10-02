module test.regression.integer_switch_disjoint_move;

protected i32 choose(i32 selected) {
    own i32* retained = new i32(59);
    switch (selected) {
    case 0:
        own i32* consumed = move retained;
        drop consumed;
        return 7;
    default:
        if (*retained != 59) {
            return 1;
        }
        return 0;
    }
}

i32 main() {
    i32 result = choose(1);
    return result;
}
