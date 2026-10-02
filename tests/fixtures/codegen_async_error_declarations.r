module test.codegen.async_error_declarations;

protected async i32 step() { return 1; }

error Empty {};

error Reason { Missing, Invalid, };

error Failure {
    own i32* payload;
    i32 code;
};

struct error { i32 value; };

protected void raise_reason(bool missing) throws Reason {
    throw (missing == true) Reason::Missing else Reason::Invalid;
}

protected void raise_empty() throws Empty {
    throw Empty {};
}

protected void rethrow_owner(Failure error) throws Failure {
    try {
        throw move error;
    } catch (Failure caught) {
        caught.code += 1;
        throw;
    }
}

async i32 main() {
    error named = {.value=7};
    i32 finally_count = 0;
    try {
        raise_empty();
        return 1;
    } catch (Empty error) {
        error as void;
        finally_count += 1;
    }
    try {
        raise_reason(false);
        return 2;
    } catch (Reason error) {
        if (error != Reason::Invalid) { return 3; }
    }
    try {
        task<i32> pending = step();
        i32 result = await move pending;
        if (result != 1) { return 9; }
    } catch (std.async::start_error error) { return 10; }
    try {
        try {
            Failure value = {.payload=new i32(41), .code=41};
            rethrow_owner(move value);
            return 4;
        } finally {
            finally_count += 10;
        }
    } catch (Failure error) {
        if (error.code != 42) { return 5; }
    }
    own i32* retained = new i32(9);
    try {
        throw (false) Failure {.payload=move retained, .code=1};
    } catch (Failure error) {
        return 6;
    }
    if (*retained != 9) { return 7; }
    drop retained;
    try {
        task<i32> pending = step();
        i32 result = await move pending;
        if (result != 1) { return 9; }
    } catch (std.async::start_error error) { return 10; }
    if (named.value != 7 || finally_count != 11) { return 8; }
    return 0;
}
