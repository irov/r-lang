module test.codegen.async_float_literal;

protected async f32 literal() {
    return 0x1.8p+1f32;
}

async i32 main() {
    try {
        task<f32> operation = literal();
        f32 value = await move operation;
        value as void;
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
    return 0;
}
