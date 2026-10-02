module test.codegen.static_initializers;

/* R-OBJ-0010: a block static is initialized by its constant initializer, so operators in it
   need no run-time checked helper, in ordinary, generic and async functions. */
usize by_const() {
    static const usize SIZE = 3usize * 2usize;
    return SIZE;
}
@generic<T>
usize padded() {
    static const usize SIZE = sizeof(T) + 1usize;
    return SIZE;
}
async usize later() {
    static const usize SIZE = 10usize - 3usize;
    return SIZE;
}

async i32 main() {
    if (by_const() != 6usize) { return 1; }
    if (padded::<u32>() != 5usize) { return 2; }
    if (padded::<u8>() != 2usize) { return 3; }
    if (await later() != 7usize) { return 4; }
    return 0;
}
