@deny_panic_alloc
module test.semantic.deny_panic_alloc_rc;

struct Cell {
    i32 value;
};

i32 main() {
    rc Cell counted = new rc Cell { .value = 3 };
    drop counted;
    return 0;
}
