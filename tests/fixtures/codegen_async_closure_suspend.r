module test.codegen.async_closure_suspend;
struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }
async i32 number() { return 20; }
async i32 main() {
    Tracked value = {.data = new i32(22)};
    async fn i32 work(task<i32> waiting) move(value) {
        i32 prior = await move waiting;
        return prior + *(value.data);
    }
    try {
        task<i32> pending = number();
        i32 result = await (move work).call(move pending);
        return result - 42;
    } catch (std.async::start_error failure) { return 1; }
}
