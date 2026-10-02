module regression.ternary_catch_state;

error Failure {
    i32 code;
};

i32 fail(own i32* value) throws Failure {
    throw Failure { .code = 1 };
}

i32 main() {
    own i32* retained = new i32(17);
    own i32* thrown = new i32(23);
    try {
        i32 result = false ? fail(move retained) : fail(move thrown);
        result as void;
    } catch (Failure failure) {
        failure as void;
        if (*retained != 17) {
            return 1;
        }
    }
    return 0;
}
