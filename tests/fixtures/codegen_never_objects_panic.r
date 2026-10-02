module test.codegen.never_objects_panic;

/* R-TYPE-0007: executing the initializer of a never local panics before the declaration
   completes; the placeholder of the never object is never read. */

struct Halt {
    i32 code;
    never reason;
};

i32 halt_code(const Halt* h) {
    return h->code;
}

i32 make(i32 x) {
    if (x > 0) {
        return x;
    }
    Halt h = Halt {.code = 1, .reason = panic("halt")};
    return halt_code(&h);
}

i32 main() {
    return make(0);
}
