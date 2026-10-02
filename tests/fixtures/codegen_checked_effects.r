module test.codegen.checked_effects;

error first_error {
    i32 code;
};

error second_error {
    i32 code;
};

protected i32 leaf(i32 mode) throws second_error, first_error {
    if (mode == 1) {
        throw first_error {
            .code = 11,
        };
    }
    if (mode == 2) {
        second_error error = {
            .code = 22,
        };
        throw error;
    }
    return 7;
}

protected i32 propagate(i32 mode, i32* finally_count)
    throws first_error, second_error {
    own i32* transfer_guard = new i32(99);
    try {
        i32 value = leaf(mode);
        return value;
    } finally {
        *finally_count += 1;
    }
}

protected i32 catch_first(i32* finally_count) {
    try {
        i32 value = propagate(1, finally_count);
        return value;
    } catch (first_error error) {
        if (error.code == 11) {
            return 3;
        }
        return 4;
    } catch (second_error error) {
        error as void;
        return 5;
    } finally {
        *finally_count += 10;
    }
}

protected void first_only() throws first_error {
    throw {
        .code = 11,
    };
}

protected void catch_and_rethrow(i32* finally_count) throws first_error {
    try {
        i32 value = leaf(1);
        value as void;
    } catch (first_error error) {
        error.code += 1;
        throw;
    } catch (second_error sibling) {
        sibling as void;
        *finally_count += 1000;
    } finally {
        *finally_count += 100;
    }
}

i32 main() {
    i32 success_finally = 0;
    try {
        i32 value = propagate(0, &success_finally);
        if (value != 7) {
            return 1;
        }
    } catch (first_error error) {
        error as void;
        return 2;
    } catch (second_error error) {
        error as void;
        return 3;
    }
    if (success_finally != 1) {
        return 4;
    }

    i32 caught_finally = 0;
    i32 caught_status = catch_first(&caught_finally);
    if ((caught_status != 3) || (caught_finally != 11)) {
        return 5;
    }

    i32 propagated_finally = 0;
    try {
        i32 value = propagate(2, &propagated_finally);
        value as void;
        return 6;
    } catch (first_error error) {
        error as void;
        return 7;
    } catch (second_error error) {
        if (error.code != 22) {
            return 8;
        }
    }
    if (propagated_finally != 1) {
        return 9;
    }

    i32 rethrow_finally = 0;
    try {
        catch_and_rethrow(&rethrow_finally);
        return 10;
    } catch (first_error error) {
        if (error.code != 12) {
            return 11;
        }
    }
    if (rethrow_finally != 100) {
        return 12;
    }
    return 0;
}
