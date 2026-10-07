module test.codegen.field_defaults_failures;

/* R-INIT-0004, R-OWN-0019 (L33): a field initializer that fails throws its checked error from
   the initialization that uses it; the explicit field values and the fields initialized before
   it are destroyed, and a failing default of core::take leaves the destination unchanged. */

protected bool matches(const std.string::string* source, const u8[] expected) {
    const u8[] actual = *source;
    usize actual_length = len(actual);
    usize expected_length = len(expected);
    if (actual_length != expected_length) { return false; }
    usize index = 0;
    while (index < actual_length) {
        if (actual[index] != expected[index]) { return false; }
        index += 1;
    }
    return true;
}

struct Profile {
    std.string::string id = std.string::from_str("none");
    std.string::string name = std.string::from_str("guest");
    std.string::string role = std.string::from_str("viewer");
};

std.string::string run() throws std.alloc::alloc_error {
    Profile first = Profile {.id = std.string::from_str("a")};
    Profile second = core::take(&first);
    return f"{first.name}|{second.id}|{second.role}";
}

i32 main() {
    try {
        std.string::string text = run();
        if (matches(&text, "guest|a|viewer") == false) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failed) {
        return 99;
    }
}
