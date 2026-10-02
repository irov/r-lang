module test.codegen.function_values_api;

/* R-TYPE-0054 (L28): function types in the exported signatures and fields of another module. */

error Rejected { i32 code; };

struct Route {
    str name;
    fn(i32) -> i32 throws(Rejected) handle;
};

i32 half(i32 value) throws Rejected {
    if (value % 2 != 0) { throw Rejected {.code = value}; }
    return value / 2;
}

Route route_half() { return Route {.name = "half", .handle = half}; }

fn(i32) -> i32 throws(Rejected) pick(bool strict) {
    strict as void;
    return half;
}

async i32 slow_double(i32 value) { return value * 2; }

(async fn(i32) -> i32) async_handler() { return slow_double; }
