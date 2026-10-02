module test.codegen.json_reader;
struct User {
    @json(string) u64 id;
    @json(name = "display_name", case = "ignore") std.string::string name;
    @json(optional, default = "missing") std.string::string email;
};
protected bool matches(const std.string::string* value, const u8[] expected) {
    const u8[] actual = std.string::as_bytes(value);
    if (len(actual) != len(expected)) { return false; }
    usize index = 0;
    while (index < len(actual)) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}
protected std.json::reader<std.io::input> create_reader(std.io::input source, std.json::options options)
    throws std.json::error, std.alloc::alloc_error {
    std.json::reader<std.io::input> result = std.json::new_reader(move source, options);
    return move result;
}
@generic<T: json_decode & send & unborrowed>
protected task<o<T> throws std.json::error, std.alloc::alloc_error, std.io::io_error>
next_type(const T* hint, std.json::reader<std.io::input>* reader) throws std.async::start_error {
    task<o<T> throws std.json::error, std.alloc::alloc_error, std.io::io_error> operation =
        std.json::read_next(reader, o::none);
    return move operation;
}
async i32 main() {
    std.io::input input = std.io::stdin();
    try {
        std.json::options options = {
            .max_depth = 256, .max_value_bytes = 1048576, .indent = 0,
            .reject_unknown_fields = false, .ignore_case = false,
            .mode = std.json::mode::sequence,
        };
        std.json::reader<std.io::input> reader = create_reader(move input, options);
        o<User> first = await std.json::read_next(&reader, o::none);
        switch (move first) {
        case variant o::some(move user):
            if (user.id != 18446744073709551615 || matches(&user.name, "Madrid") == false ||
                matches(&user.email, "missing") == false) { return 1; }
            break;
        case variant o::none:
            return 2;
        }
        i64 hint = 0;
        task<o<i64> throws std.json::error, std.alloc::alloc_error, std.io::io_error> operation =
            next_type(&hint, &reader);
        o<i64> next = await move operation;
        switch (move next) {
        case variant o::some(move value):
            if (value != -42) { return 3; }
            break;
        case variant o::none:
            return 4;
        }
        std.json::detached<std.io::input> detached = std.json::detach(move reader);
        bytes remainder = std.json::take_bytes(&detached);
        if (len(remainder) != 7) { return 5; }
        std.io::input recovered = std.json::take_handle(&detached);
        drop recovered;
        return 0;
    } catch (std.json::error failure) { return 98; }
      catch (std.alloc::alloc_error failure) { return 99; }
      catch (std.async::start_error failure) { return 97; }
      catch (std.io::io_error failure) { return 96; }
}
