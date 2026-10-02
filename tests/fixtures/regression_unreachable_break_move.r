module regression.unreachable_break_move;

i32 main() {
    own i32* owner = new i32(17);
    while (true) {
        if (false) {
            break;
        }
        own i32* consumed = move owner;
        if (*consumed != 17) {
            return 1;
        }
        break;
    }
    own i32* second_owner = new i32(23);
    while (true) {
        if (true) {
            own i32* second_consumed = move second_owner;
            if (*second_consumed != 23) {
                return 2;
            }
            break;
        } else {
            break;
        }
    }
    return 0;
}
