module test.semantic.throw_named_move_without_move;

error move_error {
    own i32* payload;
};

protected void invalid(move_error error) throws move_error {
    throw error;
}
