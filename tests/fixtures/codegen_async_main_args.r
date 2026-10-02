module test.codegen.async_main_args;

protected async i32 child() {
    return 0;
}

async i32 main(const str[] args) {
    usize argument_count = len(args);
    if (argument_count != 2) {
        return 7;
    }
    str argument = args[1];
    usize argument_length = len(argument);
    if (argument_length != 8) {
        return 9;
    }
    try {
        task<i32> operation = child();
        i32 result = await move operation;
        return result;
    } catch (std.async::start_error error) {
        error as void;
        return 8;
    }
}
