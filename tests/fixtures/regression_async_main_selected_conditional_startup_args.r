module regression.async_main_selected_conditional_startup_args;

protected async i32 child() {
    return 0;
}

async i32 main(const str[] args) {
    bool use_args = len(args) > 0;
    try {
        i32 waited = await child();
        usize selected = use_args == true ? len(args) : 0;
        selected as void;
        return waited;
    } catch (std.async::start_error failure) {
        failure as void;
        return 1;
    }
}
