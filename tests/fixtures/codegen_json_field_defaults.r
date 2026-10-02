module test.codegen.json_field_defaults;

/* R-JSON-0001, R-INIT-0004 (L33): an optional JSON field without `default` takes its declared
   initializer, a literal or a call of a zero-argument factory, when the key is absent; a present
   key replaces it, and an R initialization that omits the field evaluates the initializer. */

protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = std.string::as_bytes(source);
    usize actual_length = len(actual);
    usize expected_length = len(expected);
    if (actual_length != expected_length) { return false; }
    usize index = 0;
    while (index < actual_length) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

thread_local i32 calls = 0;

std.string::string default_name() throws std.alloc::alloc_error {
    calls += 1;
    return std.string::from_str("guest");
}

@derive(equal, format)
struct Profile {
    std.string::string id;
    @json(optional) u32 retries = 3;
    @json(optional) std.string::string name = default_name();
    @json(optional) bool verbose = true;
    u32 level = 2;
};

i32 run() throws std.json::error, std.alloc::alloc_error {
    Profile p = std.json::unmarshal("{\"id\":\"a\",\"level\":5}");
    if ((p.retries != 3u32) || (p.verbose == false) || (p.level != 5u32)) { return 1; }
    if ((matches(&p.name, "guest") == false) || (calls != 1)) { return 2; }
    Profile q = std.json::unmarshal("{\"id\":\"b\",\"level\":1,\"retries\":9,\"name\":\"x\"}");
    if ((q.retries != 9u32) || (calls != 1) || (matches(&q.name, "x") == false)) { return 3; }
    Profile r = Profile {.id = std.string::from_str("c")};
    if ((r.retries != 3u32) || (r.level != 2u32) || (calls != 2)) { return 4; }
    std.string::string text = f"{r}";
    if (matches(&text, "Profile { id: c, retries: 3, name: guest, verbose: true, level: 2 }") == false) {
        return 5;
    }
    return 0;
}

i32 main() {
    try {
        return run();
    } catch (std.json::error failure) {
        return 90;
    } catch (std.alloc::alloc_error failure) {
        return 91;
    }
}
