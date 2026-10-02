module test.regression.async_variant_standard_call;

/* M26-5: in async code a variant whose payload is the result of a standard operation was
   rejected as not lowerable. */

async u32 lengths(array<std.string::string> items) throws std.error::fault {
    o<std.string::string> taken = o::none;
    if (len(items) > 1usize) { taken = o::some(core::replace(&items[1usize], std.string::create())); }
    o<std.string::string> made = o::some(std.string::from_str("abc"));
    u32 total = 0u32;
    switch (move taken) {
    case variant o::some(move value): total += std.string::len(&value) as u32;
    case variant o::none: break;
    }
    switch (move made) {
    case variant o::some(move value): total += std.string::len(&value) as u32;
    case variant o::none: break;
    }
    drop items;
    return total;
}

async i32 main() {
    array<std.string::string> items = std.env::arguments();
    u32 total = await lengths(move items);
    return (total as i32) - 3;
}
