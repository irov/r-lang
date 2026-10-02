module test.semantic.rethrow_after_drop;

error move_error {
    own i32* payload;
};

protected void fail() throws move_error {
    own i32* payload = new i32(1);
    throw {
        .payload = move payload,
    };
}

protected void invalid_rethrow() throws move_error {
    try {
        fail();
    } catch (move_error error) {
        drop error;
        throw;
    }
}
