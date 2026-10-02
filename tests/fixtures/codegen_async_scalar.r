module test.codegen.async_scalar;

protected async char child() {
    return '\u{1f680}';
}

async i32 main() {
    try {
        task<char> operation = child();
        char value = await move operation;
        value as void;
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 3;
    }
}
