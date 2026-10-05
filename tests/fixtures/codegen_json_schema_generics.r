module test.codegen.json_schema_generics;

/* R-SLIB-JSON-0002 (M38): std.json::schema::<T>() in a generic body whose parameter has the
   json_encode or json_decode constraint is the schema of each instantiation, also when T is
   nested in another type and when one generic calls another. */

struct Point {
    i32 x;
    @json(name = "Y", optional) i32 y = 0;
};

struct Named {
    std.string::string name;
    o<u16> port;
};

protected bool same(const std.string::string* text, str expected) {
    const u8[] wanted = expected;
    return std.bytes::equal(text->as_bytes(), wanted);
}

@generic<T: json_decode>
std.string::string schema_text() throws std.json::error, std.alloc::alloc_error {
    std.json::value described = std.json::schema::<T>();
    return std.json::stringify(&described);
}

@generic<T: json_encode>
std.string::string list_schema_text() throws std.json::error, std.alloc::alloc_error {
    std.json::value described = std.json::schema::<array<T>>();
    return std.json::stringify(&described);
}

@generic<T: json_decode>
std.string::string through() throws std.json::error, std.alloc::alloc_error {
    return schema_text::<T>();
}

i32 run() throws std.json::error, std.alloc::alloc_error {
    std.json::value point = std.json::schema::<Point>();
    std.string::string direct = std.json::stringify(&point);
    std.string::string generic = schema_text::<Point>();
    if (same(&generic, direct.as_str()) == false) { return 1; }
    std.json::value named = std.json::schema::<Named>();
    std.string::string named_direct = std.json::stringify(&named);
    std.string::string named_generic = schema_text::<Named>();
    if (same(&named_generic, named_direct.as_str()) == false) { return 2; }
    if (same(&named_generic, generic.as_str()) == true) { return 3; }
    std.json::value points = std.json::schema::<array<Point>>();
    std.string::string points_direct = std.json::stringify(&points);
    std.string::string points_generic = list_schema_text::<Point>();
    if (same(&points_generic, points_direct.as_str()) == false) { return 4; }
    std.string::string nested = through::<Named>();
    if (same(&nested, named_direct.as_str()) == false) { return 5; }
    std.string::string scalar = schema_text::<u16>();
    if (same(&scalar, "{\"type\":\"integer\",\"minimum\":0,\"maximum\":65535}") == false) { return 6; }
    return 0;
}

i32 main() {
    try {
        return run();
    } catch (std.json::error failure) {
        (move failure) as void;
        return 90;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 91;
    }
}
