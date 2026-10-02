module test.semantic.stack_recursive_standard;

/* R-FUNC-0004: self-nesting through a standard type has no iterative drop. */
struct Node {
    i32 value;
    std.sync::once_lock<own Node*> cell;
};

i32 main() {
    return 0;
}
