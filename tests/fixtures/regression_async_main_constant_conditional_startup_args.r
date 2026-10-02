module regression.async_main_constant_conditional_startup_args;

protected async i32 child() {
    return 0;
}

async i32 main(const str[] args) {
    try {
        i32 waited = await child();
        usize first = true ? 0 : len(args);
        usize second = false ? len(args) : 0;
        first as void;
        second as void;
        return waited;
    } catch (std.async::start_error failure) {
        failure as void;
        return 1;
    }
}
