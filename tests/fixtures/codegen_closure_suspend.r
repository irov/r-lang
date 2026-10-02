module test.codegen.closure_suspend;
struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }
@generic<F: fn once() -> i32 & send & unborrowed>
async i32 execute(F operation, task<i32> waiting) {
    i32 prior = await move waiting;
    i32 value = (move operation).call();
    return prior + value;
}
async i32 number() { return 20; }
async i32 main() {
    Tracked value = {.data=new i32(22)};
    fn once i32 work() move(value) { return *(value.data); }
    try {
        task<i32> pending = number();
        i32 result = await execute(move work, move pending);
        return result - 42;
    } catch (std.async::start_error failure) { return 1; }
}
