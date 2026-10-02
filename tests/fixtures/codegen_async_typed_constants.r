module test.codegen.async_typed_constants;

/* R-TYPE-0047: typed constant parameters in asynchronous generic functions. */

@generic<const bool DOUBLE, const u32 BASE>
async u32 scaled(u32 value) {
    @if (DOUBLE == true) {
        return (value + BASE) * 2u32;
    } @else {
        return value + BASE;
    }
}

async i32 main() {
    try {
        task<u32> first = scaled::<true, 10u32>(1u32);
        u32 doubled = await move first;
        bool doubled_ok = doubled == 22u32;
        task<u32> second = scaled::<false, 10u32>(1u32);
        u32 plain = await move second;
        bool plain_ok = plain == 11u32;
        if (doubled_ok == false || plain_ok == false) {
            return 1;
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 2;
    }
}
