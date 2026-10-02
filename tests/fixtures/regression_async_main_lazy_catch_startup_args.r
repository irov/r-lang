module regression.async_main_lazy_catch_startup_args;

error Failure {
    i32 code;
};

protected i32 fail_number() throws Failure {
    throw Failure { .code = 1 };
}

protected bool fail_bool() throws Failure {
    throw Failure { .code = 2 };
}

protected async i32 child() {
    return 0;
}

async i32 main(const str[] args) {
    try {
        i32 waited = await child();
        i32 number = true ? 0 : fail_number();
        bool predicate = false && fail_bool();
        number as void;
        predicate as void;
        return waited;
    } catch (Failure failure) {
        usize count = len(args);
        failure as void;
        count as void;
        return 2;
    } catch (std.async::start_error failure) {
        failure as void;
        return 1;
    }
}
