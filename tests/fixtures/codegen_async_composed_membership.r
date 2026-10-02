module test.async_composed_membership;
async i32 read(i32 value) { return value; }
async i32 main() {
    try {
        bool inside = await read(5) in await read(0)..await read(10);
        if (inside == false) { return 1; }
        bool below = await read(-1) in await read(0)..await read(10);
        i32 selected = below == false ? 0 : 2;
        return selected;
    } catch (std.async::start_error failure) { return 3; }
}
