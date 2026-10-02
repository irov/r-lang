@deny_panic_alloc
module test.semantic.deny_panic_alloc_arc;

i32 main() {
    arc i32 shared = new arc i32(2);
    drop shared;
    return 0;
}
