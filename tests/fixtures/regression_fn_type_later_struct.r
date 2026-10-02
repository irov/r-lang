module test.regression.fn_type_later_struct;

/* M27-3: a function type in a field was checked before the members of a struct among its
   parameters were collected when that struct came later in collection order, so a Move struct
   parameter was rejected as unsupported. */

struct holder { std.string::string name; };

struct table { o<async fn(holder, upgrade_x) -> u32 throws(std.error::fault)> entry; };

struct upgrade_x { std.string::string text; bytes buffered; };

protected async u32 measure(holder first, upgrade_x second) throws std.error::fault {
    return (std.string::len(&first.name) + std.string::len(&second.text) + len(second.buffered)) as u32;
}

async i32 main() {
    table entries = {.entry = o::some(measure)};
    try {
        switch (entries.entry) {
        case variant o::some(chosen):
            auto call = *chosen;
            bytes extra = {};
            std.bytes::append_u8(&extra, 1u8);
            holder first = {.name = std.string::from_str("ab")};
            upgrade_x second = {.text = std.string::from_str("cde"), .buffered = move extra};
            u32 total = await call(move first, move second);
            return (total as i32) - 6;
        case variant o::none: return 1;
        }
    } catch (std.error::fault failed) {
        failed as void;
    }
    return 2;
}
