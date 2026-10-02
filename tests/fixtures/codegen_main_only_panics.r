module test.codegen.main_only_panics;

/* R-ERR-0004, R-FUNC-0003: a function with checked effects whose body only panics, including
   main with its implicit throws set, leaves its checked-effect output unused. */

error Refused {
    i32 code;
};

protected i32 refuse(i32 code) throws Refused {
    panic("refused");
}

i32 main(const str[] args) {
    if (len(args) > 100usize) {
        try {
            return refuse(1);
        } catch (Refused failure) {
            return failure.code;
        }
    }
    panic("main stops");
}
