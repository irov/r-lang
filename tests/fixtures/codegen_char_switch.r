module test.codegen.char_switch;

i32 classify(char value) {
    switch (value) {
    case 'a':
        return 1;
    case 'z':
        return 2;
    default:
        return 0;
    }
}

i32 main() {
    if (classify('a') != 1) {
        return 1;
    }
    if (classify('z') != 2) {
        return 2;
    }
    if (classify('R') != 0) {
        return 3;
    }
    return 0;
}
