module test.semantic.rethrow_after_move;

error cleanup_error {
    own i32* owner;
};

protected void consume(cleanup_error error) {
    drop error;
}

protected void invalid_rethrow(own i32* owner) throws cleanup_error {
    try {
        throw {
            .owner = move owner,
        };
    } catch (cleanup_error error) {
        consume(move error);
        throw;
    }
}
