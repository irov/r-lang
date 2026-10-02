module test.codegen.scoped_borrows;
async void pause() {}
@scoped
async void add(i32* value) throws std.async::start_error {
    *value += 1;
    await pause();
    *value += 1;
}
@scoped
async i32 inspect(const i32* value) throws std.async::start_error {
    await pause();
    return *value;
}
async i32 main() {
    try {
        i32 value = 19;
        task_scope(2) group {
            auto first = inspect(&value);
            auto second = inspect(&value);
            i32 x = await move first;
            i32 y = await move second;
            if (x + y != 38) { return 1; }
        }
        task_scope(1) updates { await add(&value); }
        i32 selected = value == 21 ? 0 : 3;
        return selected;
    } catch (std.async::start_error failure) { return 2; }
}
