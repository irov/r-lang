module test.codegen.char_compare;

/* R-EXPR-0009: char supports every comparison by scalar value. */
protected bool before(char left, char right) {
    return left < right;
}

i32 main() {
    char first = 'a';
    char second = 'b';
    char again = 'a';
    bool same = first == again;
    if (same != true) {
        return 1;
    }
    if (first == second) {
        return 2;
    }
    if (first >= second) {
        return 3;
    }
    if (second <= first) {
        return 4;
    }
    if (second > 'λ') {
        return 5;
    }
    bool ordered = before(first, second);
    if (ordered != true) {
        return 6;
    }
    if (first != again) {
        return 7;
    }
    return 0;
}
