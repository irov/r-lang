module regression.async_main_finally_startup_args;

protected async i32 child() {
    return 7;
}

async i32 main(const str[] args) {
    try {
        i32 result = await child();
        return result;
    } catch (std.async::start_error failure) {
        failure as void;
        return 4;
    } finally {
        len(args) as void;
    }
}
