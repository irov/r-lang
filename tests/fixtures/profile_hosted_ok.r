module profile.hosted_ok;

i32 main() {
    try {
        std.string::string text = std.string::from_str("text");
        if (std.string::len(&text) != 3usize) {
            return 1;
        }
    } catch (std.alloc::alloc_error failure) {
        return 2;
    }
    return 0;
}
