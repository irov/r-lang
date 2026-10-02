module test.async_call_projection;
struct Reading { own i32* storage; i32 value; };
drop(Reading* self) { *(self->storage) = 9; }
async Reading create(i32 value) { return Reading {.storage = new i32(0), .value = value}; }
async i32 selected(bool flag) throws std.async::start_error {
    i32 value = flag == true ? 42 : 7;
    return (await create(value)).value;
}
async i32 main() {
    try { i32 chosen = await selected(true) == 42 && await selected(false) == 7 ? 0 : 1; return chosen; }
    catch (std.async::start_error failure) { return 2; }
}
