module example.json.hooks;

struct UserId { u64 value; };

// Each direction is independent and owns its result.
std.json::value UserId::json_marshal(const UserId* value)
    throws std.json::error, std.alloc::alloc_error {
    std.string::string text = std.json::marshal(&value->value);
    const u8[] source = text.as_bytes();
    std.json::value result = std.json::parse(source);
    return move result;
}

UserId UserId::json_unmarshal(const std.json::value* value)
    throws std.json::error, std.alloc::alloc_error {
    std.string::string text = value->stringify();
    const u8[] source = text.as_bytes();
    u64 number = std.json::unmarshal(source);
    UserId result = {.value = number};
    return result;
}

bool UserId::json_is_zero(const UserId* value) { return value->value == 0; }

struct User {
    @json(string, omitzero)
    UserId id;
};

async i32 example() throws std.json::error, std.alloc::alloc_error {
    User user = std.json::unmarshal("{\"id\":\"18446744073709551615\"}");
    if (user.id.value != 18446744073709551615u64) { return 1; }
    std.string::string text = std.json::marshal(&user);
    const u8[] source = text.as_bytes();
    User again = std.json::unmarshal(source);
    if (again.id.value != user.id.value) { return 2; }
    User zero = {.id = {.value = 0}};
    std.string::string empty = std.json::marshal(&zero);
    const u8[] object = empty.as_bytes();
    if (len(object) != 2) { return 3; }
    return 0;
}

async i32 main() {
    try { i32 result = await example(); return result; }
    catch (std.json::error failure) { return 98; }
}
