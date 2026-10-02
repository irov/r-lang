module bench.json_parse;

/* Parse one 700-byte document into a value tree 200 000 times; every tree is dropped. */
i32 main() {
    try {
        std.string::string text = std.string::from_str("{\"users\":[{\"id\":1,\"name\":\"alice\",\"tags\":[\"admin\",\"ops\"],\"active\":true,\"score\":12.5},{\"id\":2,\"name\":\"bob\",\"tags\":[],\"active\":false,\"score\":7},{\"id\":3,\"name\":\"carol\",\"tags\":[\"dev\"],\"active\":true,\"score\":99.25}],\"page\":{\"number\":4,\"size\":50,\"total\":1234,\"next\":null},\"labels\":[\"alpha\",\"beta\",\"gamma\",\"delta\",\"epsilon\"],\"matrix\":[[1,2,3],[4,5,6],[7,8,9]],\"flags\":{\"verbose\":false,\"dry_run\":true,\"retries\":3,\"timeout\":30.5},\"description\":\"a short description of the payload used by the benchmark\"}");
        const u8[] source = text.as_bytes();
        source as void;
        usize iterations = 200_000usize;
        usize total = 0usize;
        for (usize index = 0usize; index < iterations; index += 1usize) {
            std.json::value root = std.json::parse(source);
            total += root.len();
        }
        return (total % 109usize) as i32;
    } catch (std.json::error failure) { return 65; }
}
