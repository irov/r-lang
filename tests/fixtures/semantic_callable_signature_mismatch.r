module test.semantic.callable_signature_mismatch;

/* R-TYPE-0044: the closure argument shall fit the callable constraint after the parameters
   named by the constraint are inferred from its signature. */
@generic<U, F: fn(i32) -> U>
U apply(const F* function, i32 x) {
    U r = function(x);
    return r;
}

i32 main() {
    fn bool wide(i64 x) { return x > 0; }
    bool r = apply(&wide, 1);
    r as void;
    return 0;
}
