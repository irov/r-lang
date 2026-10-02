module regression.async_main_nested_rethrow_startup_args;

error Failure {
    i32 code;
};

protected async i32 child() throws Failure {
    throw Failure { .code = 1 };
}

async i32 main(const str[] args) {
    try {
        try {
            i32 result = await child();
            return result;
        } catch (Failure failure) {
            failure as void;
            throw;
        }
    } catch (Failure failure) {
        failure as void;
        if (len(args) == 0) {
            return 1;
        }
        return 2;
    } catch (std.async::start_error failure) {
        failure as void;
        return 3;
    }
}
