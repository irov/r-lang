module example.json;

struct ConfigurationHolder { Configuration value; };

struct Address { std.string::string city; };

struct Configuration {
    @json(string)
    u64 id;

    @json(name = "display_name", case = "ignore")
    std.string::string name;

    @json(embed)
    Address address;

    @json(optional, omitempty)
    std.string::string email;

    @json(optional, default = 3)
    u32 retries;

    @json(optional, omitzero)
    bool verbose;

    @json(optional, omitnone)
    o<std.string::string> token;

    @json(skip, default = "local")
    std.string::string origin;
};

protected bool matches(const std.string::string* value, const u8[] expected) {
    const u8[] actual = *value;
    if (len(actual) != len(expected)) { return false; }
    usize i = 0;
    while (i < len(actual)) {
        if (actual[i] != expected[i]) { return false; }
        i += 1;
    }
    return true;
}

async i32 example() throws std.json::error, std.alloc::alloc_error {
    ConfigurationHolder holder = {.value = std.json::unmarshal(
        "{\"id\":\"18446744073709551615\",\"DISPLAY-NAME\":\"Ada\",\"city\":\"Madrid\"}")};
    if (holder.value.retries != 3) { return 1; }

    std.string::string encoded = std.json::marshal(&holder.value);
    bool correct = matches(&encoded,
        "{\"id\":\"18446744073709551615\",\"display_name\":\"Ada\",\"city\":\"Madrid\",\"retries\":3}");
    if (correct == false) { return 2; }

    // A failed replacement preserves the existing owner and its fields.
    try {
        holder.value = std.json::unmarshal("{\"id\":\"1\",\"display_name\":null}");
        return 3;
    } catch (std.json::error failure) {
        correct = matches(&holder.value.name, "Ada");
        if (correct == false) { return 4; }
    }

    std.json::value tree = std.json::object();
    std.json::value enabled = std.json::from_bool(true);
    tree.insert("enabled", move enabled);
    {
        o<const std.json::value*> found = tree.find("enabled");
        switch (found) {
        case variant o::some(move value):
            bool flag = value->boolean();
            if (flag == false) { return 5; }
            break;
        case variant o::none:
            return 6;
        }
    }
    std.json::value taken = tree.take_field("enabled");
    tree.insert("active", move taken);
    return 0;
}

async i32 main() {
    try {
        i32 status = await example();
        return status;
    } catch (std.json::error failure) {
        return 12;
    }
}

