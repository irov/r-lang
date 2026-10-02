module test.async_match_projection;
struct Reading { own i32* storage; i32 value; };
drop(Reading* self) { *(self->storage) = 9; }
async Reading create(i32 value) { return Reading {.storage = new i32(0), .value = value}; }
async i32 selected(bool flag) throws std.async::start_error {
    return match(flag) {
        case true: await create(42);
        case false: await create(7);
    }.value;
}
async i32 main() {
    try { i32 chosen = await selected(true) == 42 && await selected(false) == 7 ? 0 : 1; return chosen; }
    catch (std.async::start_error failure) { return 2; }
}
