module test.regression.constant_switch_catch_state;

error Failure {
    own i32* payload;
};

i32 main() {
    own i32* retained = new i32(17);
    own i32* thrown = new i32(23);
    try {
        switch (1) {
            case 0:
                throw Failure { .payload = move retained };
            case 1:
                break;
            default:
                return 2;
        }
        throw Failure { .payload = move thrown };
    } catch (Failure failure) {
        if (*(failure.payload) != 23 || *retained != 17) {
            return 1;
        }
    }
    return 0;
}
