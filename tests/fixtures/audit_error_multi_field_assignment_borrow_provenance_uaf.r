module audit.error_multi_field_assignment_borrow_provenance_uaf;

error Failure {
    own i32* left;
    own i32* right;
    const i32* left_alias;
    const i32* right_alias;
};

i32 main() {
    i32 initial_left = 0;
    i32 initial_right = 0;
    own i32* left = new i32(7);
    own i32* right = new i32(9);
    Failure failure = Failure {
        .left = move left,
        .right = move right,
        .left_alias = &initial_left,
        .right_alias = &initial_right,
    };
    failure.left_alias = &*failure.left;
    failure.right_alias = &*failure.right;
    failure.left = new i32(11);
    return *failure.left_alias;
}
