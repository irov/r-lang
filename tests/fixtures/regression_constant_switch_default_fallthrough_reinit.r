module test.regression.constant_switch_default_fallthrough_reinit;

i32 main() {
    own i32* owner = new i32(17);
    own i32* consumed = move owner;
    if (*consumed != 17) {
        return 2;
    }
    switch (1) {
        default:
            owner = new i32(23);
            fallthrough;
        case 1:
            if (*owner == 23) {
                return 0;
            }
            return 3;
    }
}
