module test.codegen.never_objects;

/* R-TYPE-0007, R-AGG-0001, R-FUNC-0003: a local, a parameter, a struct field and a variant
   payload of type never. Only an expression that does not complete initializes such an object,
   so its declaration ends the reachable path; a struct with a never field and a variant with a
   never payload are never constructed; reading a never object converts to any type. */

struct Halt {
    i32 code;
    never reason;
};

enum Step {
    Run(i32),
    Stop(never),
};

never fail(str message) {
    panic(message);
}

i32 halt_code(const Halt* h) {
    return h->code;
}

i32 reason_of(Halt h) {
    i32 reason = h.reason;
    return reason;
}

i32 take(never value) {
    return value;
}

i32 run(Step s) {
    switch (s) {
        case variant Step::Run(v):
            return *v;
        case variant Step::Stop(n):
            return 0;
    }
}

/* The declaration of a never local ends the path, so no return follows it. */
i32 local_end(i32 x) {
    if (x > 0) {
        return x;
    }
    never halted = fail("local");
}

i32 local_read(i32 x) {
    if (x > 0) {
        return x;
    }
    never halted = panic("read");
    i32 y = halted;
    return y;
}

i32 make(i32 x) {
    if (x > 0) {
        return run(Step::Run(x));
    }
    Halt h = Halt {.code = 1, .reason = fail("halt")};
    return halt_code(&h) + reason_of(h);
}

i32 stop(i32 x) {
    if (x > 0) {
        return x;
    }
    return run(Step::Stop(fail("stop")));
}

i32 call(i32 x) {
    if (x > 0) {
        return x;
    }
    return take(fail("take"));
}

i32 main() {
    return local_end(1) + local_read(2) + make(3) + stop(4) + call(5) - 15;
}
