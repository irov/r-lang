module regression.async_main_short_circuit_startup_args;

protected async i32 child() {
    return 0;
}

async i32 main(const str[] args) {
    try {
        i32 waited = await child();
        bool left = false && (len(args) > 0);
        bool right = true || (len(args) > 0);
        left as void;
        right as void;
        return waited;
    } catch (std.async::start_error failure) {
        failure as void;
        return 1;
    }
}
