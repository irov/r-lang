module test.regression.condition_constant_short_circuit_move;

protected bool consume(own i32* value) {
    drop value;
    return true;
}

i32 main() {
    own i32* retained = new i32(71);
    if (false && consume(move retained) == true) {
        return 1;
    }
    i32 result = *retained;
    drop retained;
    i32 selected = result == 71 ? 0 : 2;
    return selected;
}
