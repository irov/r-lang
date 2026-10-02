module example.json_tool.config;
import example.json_tool.tree;

struct CommandStorage1 { Configuration value; };
struct CommandStorage3 { bool value; };

struct UserId { u64 value; };

std.json::value UserId::json_marshal(const UserId* value) throws std.json::error, std.alloc::alloc_error {
    std.string::string text = std.json::marshal(&value->value);
    const u8[] source = text.as_bytes();
    std.json::value result = std.json::parse(source);
    return move result;
}

UserId UserId::json_unmarshal(const std.json::value* value) throws std.json::error, std.alloc::alloc_error {
    std.string::string text = value->stringify();
    const u8[] source = text.as_bytes();
    u64 number = std.json::unmarshal(source);
    return UserId { .value = number };
}

bool UserId::json_is_zero(const UserId* value) { return value->value == 0u64; }
UserId default_id() { return UserId { .value = 0u64 }; }

struct Location { std.string::string city; };
struct Configuration {
    @json(optional, default = default_id, string, omitzero) UserId id;
    @json(name = "display_name", case = "ignore") std.string::string name;
    @json(embed) Location address;
    @json(optional, omitempty) std.string::string email;
    @json(optional, default = 3) u32 retries;
    @json(optional, omitzero) bool verbose;
    @json(optional, omitnone) o<std.string::string> token;
    @json(skip, default = "local") std.string::string origin;
};

@generic<T: json_encode & json_decode>
struct Snapshot { T configuration; };

// Decode text as T and encode it again. T occurs only inside, so callers name it explicitly.
@generic<T: json_encode & json_decode>
std.string::string canonical(str source, std.json::options options)
    throws std.json::error, std.alloc::alloc_error {
    T value = std.json::unmarshal_with_options(source, options);
    return std.json::marshal_with_options(&value, options);
}

std.string::string normalize(str source, bool strict) throws std.json::error, std.alloc::alloc_error {
    std.json::options options = example.json_tool.tree::options(2u32, strict, std.json::mode::document);
    return canonical::<Configuration>(source, options);
}

std.string::string snapshot(str source) throws std.json::error, std.alloc::alloc_error {
    Snapshot<Configuration> captured = std.json::unmarshal(source);
    return std.json::marshal(&captured);
}

// Decode the replacement completely before committing it to the existing configuration.
std.string::string reload(str base, str update) throws std.json::error, std.alloc::alloc_error {
    CommandStorage1 state_settings = {.value = std.json::unmarshal(base)};
    CommandStorage3 state_replaced = {.value = false};
    try { state_settings.value = std.json::unmarshal(update); state_replaced.value = true; }
    catch (std.json::error failure) { }
    std.string::string serialized = std.json::marshal(&state_settings.value);
    return f"replaced={state_replaced.value}\n{serialized}";
}
