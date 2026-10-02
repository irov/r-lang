module audit.std_c_handle_access;

raw void* inspect(const std.c::handle* handle) {
    return std.c::handle_pointer(handle);
}

raw void* release(std.c::handle handle) {
    return std.c::release_handle(move handle);
}

i32 main() {
    return 0;
}
