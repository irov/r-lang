module test.codegen.stdout;

i32 main() {
    std.io::output stream = std.io::stdout();
    std.io::output selected = move stream;
    return 0;
}
