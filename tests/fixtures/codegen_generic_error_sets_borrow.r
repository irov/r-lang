module test.codegen.generic_error_sets_borrow;
error Borrowed { const i32* p; };
i32 bad(const i32* p) throws Borrowed { throw Borrowed {.p=p}; }
@generic<E: errors, F: fn(const i32*) -> i32 throws(E)>
i32 invoke(const F* f, const i32* p) throws E { return f(p); }
@generic<E: errors, F: fn(const i32*) -> i32 throws(E)>
const i32* recover(const F* f, const i32* p) throws E {
 try { f(p) as void; return p; }
 catch (Borrowed failure) { return failure.p; }
}
i32 main() { i32 x=42; auto f=bad;
 try { const i32* got = recover(&f,&x); i32 selected = *got == 42 ? 0 : 1; return selected; }
 catch (Borrowed failure) { i32 selected = *failure.p == 42 ? 0 : 2; return selected; }
}
