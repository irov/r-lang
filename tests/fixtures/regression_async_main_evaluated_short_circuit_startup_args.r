module regression.async_main_evaluated_short_circuit_startup_args;

protected async i32 child() {
    return 0;
}

async i32 main(const str[] args) {
    bool evaluate_right = len(args) > 0;
    try {
        i32 waited = await child();
        bool selected = evaluate_right && (len(args) > 0);
        selected as void;
        return waited;
    } catch (std.async::start_error failure) {
        failure as void;
        return 1;
    }
}
