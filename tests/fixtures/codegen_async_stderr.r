module test.codegen.async_stderr;

async i32 main() {
    std.io::output error = std.io::stderr();
    std.io::output moved = move error;
    return 0;
}
