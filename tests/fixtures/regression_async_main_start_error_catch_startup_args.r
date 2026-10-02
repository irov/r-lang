module regression.async_main_start_error_catch_startup_args;

protected async i32 child() {
    return 0;
}

async i32 main(const str[] args) {
    try {
        i32 result = await child();
        return result;
    } catch (std.async::start_error failure) {
        failure as void;
        if (len(args) == 0) {
            return 1;
        }
        return 0;
    }
}
