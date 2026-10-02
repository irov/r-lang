module profile.freestanding_std_string;

protected usize measure(const std.string::string* text) {
    return std.string::len(text);
}

i32 main() {
    return 0;
}
