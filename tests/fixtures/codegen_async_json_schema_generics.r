module test.codegen.async_json_schema_generics;

/* R-SLIB-JSON-0002 (M38): std.json::schema::<T>() in the body of a generic async function is
   built for each instantiation, as in a synchronous one. */

struct Point {
    i32 x;
    @json(name = "Y", optional) i32 y = 0;
};

struct Named {
    std.string::string name;
    o<u16> port;
};

protected bool same(const std.string::string* text, const std.string::string* expected) {
    return std.bytes::equal(*text, *expected);
}

/* A suspension between building the schema and reading it. */
protected async void pause() throws std.error::fault {
    try {
        await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000u32));
    } catch (std.time::duration_error rejected) {
        rejected as void;
    }
}

@generic<T: json_decode>
async std.string::string schema_text() throws std.json::error, std.error::fault {
    std.json::value described = std.json::schema::<T>();
    await pause();
    return std.json::stringify(&described);
}

@generic<T: json_encode>
async usize property_count() throws std.json::error, std.error::fault {
    std.json::value described = std.json::schema::<T>();
    await pause();
    switch (std.json::find(&described, "properties")) {
    case variant o::some(properties): return std.json::len(*properties);
    case variant o::none: return 0usize;
    }
}

async i32 run() throws std.json::error, std.error::fault {
    std.json::value point = std.json::schema::<Point>();
    std.string::string direct = std.json::stringify(&point);
    std.string::string generic = await schema_text::<Point>();
    if (same(&generic, &direct) == false) { return 1; }
    std.json::value named = std.json::schema::<Named>();
    std.string::string named_direct = std.json::stringify(&named);
    std.string::string named_generic = await schema_text::<Named>();
    if (same(&named_generic, &named_direct) == false) { return 2; }
    usize points = await property_count::<Point>();
    if (points != 2usize) { return 3; }
    usize names = await property_count::<Named>();
    if (names != 2usize) { return 3; }
    usize none = await property_count::<u16>();
    if (none != 0usize) { return 4; }
    return 0;
}

async i32 main() {
    try {
        return await run();
    } catch (std.json::error failure) {
        (move failure) as void;
        return 90;
    }
}
