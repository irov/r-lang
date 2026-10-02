module test.codegen.async_link_available;

async i32 main() {
    constexpr str logical_name = "system.libc";
    bool available = std.c::link_available(logical_name);

    if (available == true) {
        return 0;
    } else {
        return 1;
    }
}
