module test.semantic.membership_unsupported;

/* R-EXPR-0029: a type without a core::Contains implementation supports no membership test. */
struct Bag {
    i32 value;
};

i32 main() {
    Bag bag = Bag { .value = 1 };
    i32 one = 1;
    if (one in bag) { return 1; }
    return 0;
}
