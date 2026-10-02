module test.codegen.function_values_modules;
import test.codegen.function_values_api;

/* R-TYPE-0054 (L28): calls through function values that another module created. */

async i32 main() {
    test.codegen.function_values_api::Route route = test.codegen.function_values_api::route_half();
    i32 total = 0;
    try {
        total += route.handle(8);
        total += test.codegen.function_values_api::pick(true)(10);
        total += route.handle(3);
    } catch (test.codegen.function_values_api::Rejected failure) {
        total += failure.code;
    }
    async fn(i32) -> i32 later = test.codegen.function_values_api::async_handler();
    try {
        total += await later(5);
    } catch (std.async::start_error failure) {
        return 1;
    }
    if (total != 22) { return 2; }
    return 0;
}
