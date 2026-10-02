module regression.async_main_completion_catch_startup_args;

error Failure {
    i32 code;
};

protected async i32 child() throws Failure {
    throw Failure { .code = 7 };
}

async i32 main(const str[] args) {
    try {
        i32 result = await child();
        return result;
    } catch (Failure failure) {
        failure as void;
        usize count = len(args);
        if (count == 0) {
            return 2;
        }
        str program = args[0];
        usize program_length = len(program);
        if (program_length == 0) {
            return 3;
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 4;
    }
}
