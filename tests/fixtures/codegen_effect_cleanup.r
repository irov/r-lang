module test.codegen.effect_cleanup;

error first_error {
    i32 code;
};

error second_error {
    i32 code;
};

error owned_error {
    i32 code;
    array<u8> payload;
};

protected void fail(i32 selector) throws second_error, first_error {
    if (selector == 1) {
        throw first_error {
            .code = 11,
        };
    }
    if (selector == 2) {
        throw second_error {
            .code = 22,
        };
    }
}

protected void propagate(i32 selector,
                         array<u8> first,
                         array<u8> second,
                         i32* finally_count)
    throws first_error, second_error {
    try {
        fail(selector);
        drop second;
        drop first;
    } finally {
        *finally_count += 1;
    }
}

protected i32 catch_first(array<u8> outer, array<u8> inner) {
    try {
        fail(1);
        drop inner;
    } catch (first_error error) {
        drop outer;
        return error.code;
    } catch (second_error error) {
        drop outer;
        return error.code;
    }
    drop outer;
    return 0;
}

protected void fail_owned() throws owned_error {
    array<u8> payload = {};
    throw {
        .code = 33,
        .payload = move payload,
    };
}

protected i32 catch_exit_through_finally(i32* finally_count) {
    try {
        try {
            fail_owned();
        } catch (owned_error error) {
            return error.code;
        }
    } finally {
        *finally_count += 1;
    }
    return 0;
}

i32 main() {
    i32 finally_count = 0;
    try {
        array<u8> first = {};
        array<u8> second = {};
        propagate(1, move first, move second, &finally_count);
        return 1;
    } catch (first_error error) {
        if (error.code != 11) {
            return 2;
        }
    } catch (second_error error) {
        error as void;
        return 3;
    }

    try {
        array<u8> first = {};
        array<u8> second = {};
        propagate(2, move first, move second, &finally_count);
        return 4;
    } catch (first_error error) {
        error as void;
        return 5;
    } catch (second_error error) {
        if (error.code != 22) {
            return 6;
        }
    }

    try {
        array<u8> first = {};
        array<u8> second = {};
        propagate(0, move first, move second, &finally_count);
    } catch (first_error error) {
        error as void;
        return 7;
    } catch (second_error error) {
        error as void;
        return 8;
    }
    if (finally_count != 3) {
        return 9;
    }

    array<u8> outer = {};
    array<u8> inner = {};
    i32 caught = catch_first(move outer, move inner);
    if (caught != 11) {
        return 10;
    }

    i32 catch_finally_count = 0;
    i32 caught_owned = catch_exit_through_finally(&catch_finally_count);
    if ((caught_owned != 33) || (catch_finally_count != 1)) {
        return 11;
    }
    return 0;
}
