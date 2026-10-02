module regression.assignment_rhs_moves_destination;

protected own i32* pass(own i32* value) {
    return move value;
}

i32 main() {
    own i32* value = new i32(1);
    value = pass(move value);
    drop value;
    return 0;
}
