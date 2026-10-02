module test.codegen.stack_entries;

/* An acyclic chain: every entry gets a static bound and calls carry no preflight. */
struct Pair {
    i32 left;
    i32 right;
};

protected i32 leaf(i32 value) {
    return value * 2;
}

protected i32 middle(Pair pair) {
    i32 doubled = leaf(pair.left);
    return doubled + pair.right;
}

protected i32 top(i32 value) {
    Pair pair = { .left = value, .right = 1 };
    i32 result = middle(pair);
    return result;
}

i32 main() {
    i32 total = top(20);
    return total - 41;
}
