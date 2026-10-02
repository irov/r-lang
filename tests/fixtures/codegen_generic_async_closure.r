module test.codegen.generic_async_closure;
struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }
@generic<T: send & unborrowed>
async T relay(T value) throws std.async::start_error {
    async fn T local(T argument) { return move argument; }
    T result = await local(move value);
    return move result;
}
@generic<T: copy & send & unborrowed>
async T snapshot(T value) throws std.async::start_error {
    async fn T read() move(value) { return value; }
    T result = await read();
    return result;
}
async i32 main() {
    try {
        i32 number = await snapshot(42);
        Tracked first = {.data = new i32(number)};
        Tracked returned = await relay(move first);
        if (*(returned.data) != 42) { return 1; }
        Tracked second = {.data = new i32(7)};
        Tracked again = await relay(move second);
        i32 selected = *(again.data) == 7 ? 0 : 2;
        return selected;
    } catch (std.async::start_error failure) { return 3; }
}
