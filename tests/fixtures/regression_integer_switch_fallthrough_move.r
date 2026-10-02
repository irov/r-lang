module test.regression.integer_switch_fallthrough_move;

protected void choose(i32 selected) {
    own i32* retained = new i32(71);
    switch (selected) {
    case 0:
        own i32* consumed = move retained;
        drop consumed;
        fallthrough;
    case 1:
        break;
    default:
        break;
    }
}

i32 main() {
    choose(1);
    return 0;
}
