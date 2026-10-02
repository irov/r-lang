module test.codegen.async_function_values;

/* R-TYPE-0054 (L28): async function values in a struct, a local, a fixed array and a result;
   each start selects the frame of the designated function. The test wrapper rejects the first
   start after main's own, so a call through a value reports std.async::start_error and leaves
   the values usable. */

error Invalid { i32 code; };

async i32 double_it(i32 value) { return value * 2; }
async i32 negate(i32 value) { return 0 - value; }
async i32 guarded(i32 value) throws Invalid {
    if (value < 0) { throw Invalid {.code = value}; }
    return value;
}

struct Handlers {
    async fn(i32) -> i32 first;
    async fn(i32) -> i32 second;
    async fn(i32) -> i32 throws(Invalid) guard;
};

(async fn(i32) -> i32) choose(bool negative) {
    if (negative == true) { return negate; }
    return double_it;
}

async i32 main() {
    Handlers handlers = Handlers {.first = double_it, .second = negate, .guard = guarded};
    i32 total = 0;
    try {
        i32 unexpected = await handlers.first(1);
        unexpected as void;
        return 1;
    } catch (std.async::start_error failure) {
        total += 100;
    }
    try {
        total += await handlers.first(21);
        async fn(i32) -> i32 h = handlers.second;
        total += await h(2);
        total += await choose(true)(3);
        (async fn(i32) -> i32)[2] pair = {double_it, negate};
        total += await pair[0](1);
        try {
            total += await handlers.guard(-5);
        } catch (Invalid failure) {
            total += failure.code;
        }
        task_scope(2) group {
            auto a = h(4);
            auto b = pair[0](4);
            i32 x = await move a;
            i32 y = await move b;
            total += x + y;
        }
    } catch (std.async::start_error failure) {
        return 2;
    }
    if (total != 138) { return 3; }
    return 0;
}
