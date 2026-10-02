module test.regression.short_circuit_catch_state;

error Failure {
    i32 code;
};

protected bool fail(own i32* value) throws Failure {
    throw Failure { .code = 1 };
}

i32 main() {
    own i32* retained = new i32(73);
    try {
        bool skipped = false && fail(move retained);
        skipped as void;
        throw Failure { .code = 2 };
    } catch (Failure failure) {
        if (failure.code != 2 || *retained != 73) {
            return 1;
        }
    }
    return 0;
}
