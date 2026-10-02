module test.codegen.async_generic_error_sets;
error A { i32 x; };
async i32 bad(i32 x) throws A { throw A {.x=x}; }
@generic<E: errors & send & unborrowed, F: async fn once(i32) -> i32 throws(E)>
async i32 invoke(F f, i32 x) throws E, std.async::start_error { return await (move f).call(x); }
async i32 main() {
 try { i32 v=await invoke(bad,42); return v; }
 catch (A failure) { i32 selected = failure.x == 42 ? 0 : 2; return selected; }
 catch (std.async::start_error failure) { failure as void; return 3; }
}
