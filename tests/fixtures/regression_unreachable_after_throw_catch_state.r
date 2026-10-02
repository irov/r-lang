module regression.unreachable_after_throw_catch_state;

error Failure {
    i32 code;
};

i32 main() {
    own i32* retained = new i32(41);
    try {
        throw Failure { .code = 1 };
        own i32* consumed = move retained;
        throw Failure { .code = 2 };
    } catch (Failure failure) {
        if (failure.code != 1 || *retained != 41) {
            return 1;
        }
    }
    return 0;
}
