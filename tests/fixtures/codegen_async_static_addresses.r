module test.codegen.async_static_addresses;

/* Core R-OBJ-0013, R-FUNC-0011 (B11): address constants of static objects read and written from
   async functions. A raw pointer is not Send and stays out of the frame, but a block-scope static
   that holds one keeps its static storage across an await (B11-1). */

struct Counters {
    u64 hits;
    u32[4] slots;
};

Counters state = {.hits = 5u64, .slots = {1u32, 2u32, 3u32, 4u32}};
raw u32* third = &state.slots[2] as raw u32*;
raw const u64* hits = &state.hits as raw const u64*;

protected async u32 tick(u32 value) {
    return value + 1u32;
}

protected async u32 bump(u32 amount) throws std.async::start_error {
    u32 step = await tick(amount);
    unsafe {
        raw u32* target = third;
        *target += step;
        return *target;
    }
}

protected async u32 last_of_pair() throws std.async::start_error {
    static u32[2] pair = {7u32, 8u32};
    static raw u32* last = &pair[1] as raw u32*;
    u32 step = await tick(0u32);
    unsafe {
        *last += step;
        return pair[1];
    }
}

async i32 main() {
    try {
        if ((await bump(9u32)) != 13u32) {
            return 1;
        }
        unsafe {
            if ((state.slots[2] != 13u32) || (*hits != 5u64)) {
                return 2;
            }
        }
        if ((await last_of_pair()) != 9u32) {
            return 3;
        }
        if ((await last_of_pair()) != 10u32) {
            return 5;
        }
        return 0;
    } catch (std.async::start_error failure) {
        return 4;
    }
}
