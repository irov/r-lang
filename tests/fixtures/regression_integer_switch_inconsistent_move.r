module test.regression.integer_switch_inconsistent_move;

protected void choose(i32 selected) {
    own i32* retained = new i32(67);
    switch (selected) {
    case 0:
        own i32* consumed = move retained;
        drop consumed;
        break;
    default:
        break;
    }
}

i32 main() {
    choose(1);
    return 0;
}
