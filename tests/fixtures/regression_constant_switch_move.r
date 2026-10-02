module regression.constant_switch_move;

i32 main() {
    own i32* retained = new i32(31);
    switch (0) {
    case 0:
        break;
    default:
        own i32* unreachable = move retained;
        drop unreachable;
        break;
    }
    if (*retained != 31) {
        return 1;
    }
    return 0;
}
