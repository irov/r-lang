module test.codegen.enum_fallthrough;

enum Mode {
    A,
    B,
    C,
};

i32 select(Mode mode) {
    switch (mode) {
    case variant Mode::A:
        fallthrough;
    case variant Mode::B:
        return 1;
    case variant Mode::C:
        return 0;
    }
}

i32 select_constant() {
    switch (Mode::A) {
    case variant Mode::A:
        fallthrough;
    case variant Mode::B:
        return 1;
    case variant Mode::C:
        return 0;
    }
}

i32 main() {
    if (select(Mode::A) != 1) {
        return 1;
    }
    if (select(Mode::B) != 1) {
        return 2;
    }
    if (select(Mode::C) != 0) {
        return 3;
    }
    if (select_constant() != 1) {
        return 4;
    }
    return 0;
}
