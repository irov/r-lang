module test.codegen.async_owner_shapes;

@generic<T: send & unborrowed>
async T forward(T value) {
    return move value;
}

i32 sum((own i32*)[2] values) {
    i32 total = 0;
    for (auto value in &values) {
        total += **value;
    }
    return total;
}

async i32 main() {
    (own i32*)[2] values = {new i32(19), new i32(23)};
    try {
        u32[3] flags = {7u32};
        u32[3] copied = await forward(move flags);
        if (copied[0] != 7u32 || copied[1] != 0u32 || flags[0] != 7u32) { return 5; }
        (o<own i32*>)[2] empty = {};
        (o<own i32*>)[2] forwarded_empty = await forward(move empty);
        drop forwarded_empty;
        (own i32*?)[2] nullable = {};
        (own i32*?)[2] forwarded_nullable = await forward(move nullable);
        drop forwarded_nullable;
        (own i32*)[2] returned = await forward(move values);
        std.thread::join_handle<i32> worker = std.thread::spawn(sum, move returned);
        std.thread::join_result<i32> completion = std.thread::join(move worker);
        switch (move completion) {
        case variant std.thread::join_result::returned(move value):
            i32 selected = value == 42 ? 0 : 1;
            return selected;
        case variant std.thread::join_result::panicked(move report):
            return 2;
        }
    } catch (std.async::start_error failure) { return 3; }
      catch (std.thread::thread_error failure) { return 4; }
}
