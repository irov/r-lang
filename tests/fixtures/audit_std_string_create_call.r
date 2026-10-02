module audit.std_string_create_call;

i32 main() {
    std.string::string value = std.string::create();
    drop value;
    return 0;
}
