module regression.unreachable_throw_catch_state;

error Failure {
    own i32* payload;
    i32 code;
};

i32 main() {
    own i32* retained = new i32(17);
    own i32* thrown = new i32(23);
    try {
        throw (false) Failure { .payload = move retained, .code = 1 };
        throw Failure { .payload = move thrown, .code = 2 };
    } catch (Failure failure) {
        if (failure.code != 2 || *retained != 17) {
            return 1;
        }
    }
    return 0;
}
