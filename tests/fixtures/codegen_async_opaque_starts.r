module test.codegen.async_opaque_starts;

/* R-TYPE-0048: an opaque async callable is started directly like its concrete implementation;
   R-STMT-0017: a generic @scoped function takes a closure argument. */
async i32 check(i32 x) { return x + 1; }
opaque(async fn once(i32) -> i32 & copy) checker() { return check; }
opaque(async fn once(i32) -> i32) offset(i32 base) {
    async fn i32 add(i32 x) move(base) { return base + x; }
    return add;
}

async void pause() {}
@generic<F: fn(i32) -> i32 & send & unborrowed>
@scoped
async i32 scoped_apply(F f, const i32* x) throws std.async::start_error {
    await pause();
    return f(*x);
}

async i32 main() {
    try {
        auto c = checker();
        if (await c(41) != 42) { return 1; }
        auto off = offset(40);
        if (await (move off).call(2) != 42) { return 2; }
        i32 input = 20;
        i32 two = 2;
        fn i32 twice(i32 v) move(two) { return v * two + 2; }
        task_scope(1) group {
            auto job = scoped_apply(twice, &input);
            i32 result = await move job;
            if (result != 42) { return 3; }
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 7;
    }
}
