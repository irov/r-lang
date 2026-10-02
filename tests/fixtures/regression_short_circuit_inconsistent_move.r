module test.regression.short_circuit_inconsistent_move;

protected bool consume(own i32* value) {
    drop value;
    return true;
}

protected void choose(bool selected) {
    own i32* retained = new i32(61);
    bool result = selected && consume(move retained);
    result as void;
}

i32 main() {
    choose(true);
    return 0;
}
