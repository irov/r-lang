module test.codegen.async_stdin;

async i32 main() {
    std.io::input stream = std.io::stdin();
    std.io::input selected = move stream;
    drop selected;
    return 0;
}
