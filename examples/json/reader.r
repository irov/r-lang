module example.json.reader_sample;

struct User {
    @json(string) u64 id;
    @json(name = "display_name", case = "ignore") std.string::string name;
};

async i32 main() {
    try {
        std.io::input input = std.io::stdin();
        std.json::options options = {
            .max_depth = 256, .max_value_bytes = 67108864, .indent = 0,
            .reject_unknown_fields = false, .ignore_case = false,
            .mode = std.json::mode::sequence,
        };
        std.json::reader<std.io::input> reader = std.json::new_reader(move input, options);
        // Input: {"id":"18446744073709551615","Display-Name":"Madrid"} -42 "tail"
        o<User> next = await reader.read_next();
        switch (move next) {
        case variant o::some(move user):
            if (user.id != 18446744073709551615u64) { return 1; }
            break;
        case variant o::none:
            return 2;
        }
        // Between documents the contextual result type may change.
        o<i64> number = await reader.read_next();
        switch (number) {
        case variant o::some(move value):
            if (value != -42) { return 3; }
            break;
        case variant o::none:
            return 4;
        }
        // Hand back both the source and the exact unread suffix to another consumer.
        std.json::detached<std.io::input> detached = (move reader).detach();
        std.io::input recovered = detached.take_handle();
        bytes remaining = detached.take_bytes();
        drop remaining;
        await (move recovered).close();
        return 0;
    } catch (std.json::error failure) { return 98; }
}
