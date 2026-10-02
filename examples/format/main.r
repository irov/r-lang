module example.format;

error MessageError { std.string::string message; };

protected void validate(i32 count) throws MessageError, std.alloc::alloc_error {
    throw (count < 0) MessageError {.message = f"negative count: {count}"};
}

i32 main() {
    std.string::string name = std.string::from_str("Ada");
    std.string::string greeting = f"hello {name}";
    const u8[] greeting_bytes = greeting.as_bytes();
    if (len(greeting_bytes) != 9) { return 1; }

    std.format::format f0 = f"{name} {1} {2} {1}";
    drop name;
    std.string::string first = f0.format(1, 2);       // Ada 1 2 1
    const u8[] first_bytes = first.as_bytes();
    if (len(first_bytes) != 9) { return 1; }
    std.string::string second = f0.format("A", "B"); // Ada A B A
    const u8[] second_bytes = second.as_bytes();
    if (len(second_bytes) != 9) { return 1; }
    std.string::string offset = f"offset={1:08x}".format(42);
    const u8[] offset_bytes = offset.as_bytes();
    if (len(offset_bytes) != 15) { return 1; }
    f64 fraction = 1.125;
    std.string::string rounded = f"{{{fraction:.2}}}"; // {1.12}
    const u8[] rounded_bytes = rounded.as_bytes();
    if (len(rounded_bytes) != 6) { return 1; }
    try {
        validate(-3);
    } catch (MessageError error) {
        const u8[] message = std.string::as_bytes(&error.message);
        if (len(message) != 18) { return 2; }
    }
    return 0;
}
