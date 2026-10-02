module test.codegen.move_error_rethrow;

error move_error {
    own i32* payload;
    i32 code;
};

protected void fail() throws move_error {
    own i32* payload = new i32(41);
    throw {
        .payload = move payload,
        .code = 41,
    };
}

protected void throw_named(move_error error) throws move_error {
    throw move error;
}

protected void verify_named_move_throw() throws move_error {
    own i32* payload = new i32(43);
    move_error error = {
        .payload = move payload,
        .code = 43,
    };
    throw_named(move error);
}

protected void mutate_and_rethrow(i32* finally_count) throws move_error {
    try {
        fail();
    } catch (move_error error) {
        error.code += 1;
        throw;
    } finally {
        *finally_count += 1;
    }
}

i32 main() {
    i32 finally_count = 0;
    try {
        mutate_and_rethrow(&finally_count);
        return 1;
    } catch (move_error error) {
        if (finally_count != 1) {
            return 2;
        }
        if (error.code != 42) {
            return 3;
        }
    }
    try {
        verify_named_move_throw();
        return 4;
    } catch (move_error error) {
        if (error.code != 43) {
            return 5;
        }
    }
    return 0;
}
