module test.codegen.async_consteval_frozen;

/* L22.3, L22.4 (R-EXPR-0032): frozen owners and translation-time tagged values read by
   asynchronous functions, lowered through MIR. */

enum reply { accepted(u16), rejected };

dict<str, u16> routes() throws std.alloc::alloc_error, std.dict::insert_error<str, u16> {
    dict<str, u16> table = std.dict::create::<str, u16>();
    o<u16> api = table.insert("/api", 8081u16);
    api as void;
    o<u16> admin = table.insert("/admin", 9000u16);
    admin as void;
    return move table;
}

std.string::string greeting() throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("hello");
    std.string::push_scalar(&text, '!');
    return move text;
}

reply decide(u16 port) {
    if (port > 8000u16) {
        reply yes = reply::accepted(port);
        return yes;
    }
    reply no = reply::rejected;
    return no;
}

const dict<str, u16> ROUTES = routes();
const std.string::string GREETING = greeting();
const reply ADMIN = decide(9000u16);

async u16 route(bool api) {
    str path = "/none";
    if (api == true) {
        path = "/api";
    }
    o<const u16*> found = ROUTES.get(&path);
    return match (found) {
        case variant o::some(port): *port;
        case variant o::none: 0u16;
    };
}

async i32 main() {
    i32 failures = 0;
    u16 api = await route(true);
    failures += api == 8081u16 ? 0 : 1;
    u16 missing = await route(false);
    failures += missing == 0u16 ? 0 : 1;
    failures += std.string::len(&GREETING) == 6usize ? 0 : 1;
    u16 admitted = match (ADMIN) {
        case variant reply::accepted(port): port;
        case variant reply::rejected: 0u16;
    };
    failures += admitted == 9000u16 ? 0 : 1;
    return failures;
}
