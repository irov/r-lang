module test.regression.short_circuit_constant_move;

protected bool consume(own i32* value) {
    drop value;
    return true;
}

protected i32 false_and() {
    own i32* retained = new i32(41);
    bool result = false && consume(move retained);
    result as void;
    return *retained;
}

protected i32 true_or() {
    own i32* retained = new i32(43);
    bool result = true || consume(move retained);
    result as void;
    return *retained;
}

i32 main() {
    i32 first = false_and();
    i32 second = true_or();
    second as void;
    if (first != 41 || second != 43) {
        return 1;
    }
    return 0;
}
