module test.codegen.async_short_circuit;

async i32 main() {
    bool or_false = false || false;
    bool or_right = false || true;
    bool or_left = true || false;
    bool and_left = false && true;
    bool and_right = true && false;
    bool and_true = true && true;
    i32 zero = 0;
    bool lazy_or = true || ((1 / zero) == 0);
    bool lazy_and = false && ((1 / zero) == 0);

    if (or_false != false) {
        return 1;
    }
    if (or_right != true) {
        return 2;
    }
    if (or_left != true) {
        return 3;
    }
    if (and_left != false) {
        return 4;
    }
    if (and_right != false) {
        return 5;
    }
    if (and_true != true) {
        return 6;
    }
    if (lazy_or != true) {
        return 7;
    }
    if (lazy_and != false) {
        return 8;
    }
    return 0;
}
