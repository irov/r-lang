module regression.async_main_pre_suspend_catch_startup_args;

error EarlyFailure {
    i32 code;
};

error LateFailure {
    i32 code;
};

protected i32 before_suspend() throws EarlyFailure {
    throw EarlyFailure { .code = 1 };
}

protected async i32 after_suspend() throws LateFailure {
    throw LateFailure { .code = 2 };
}

async i32 main(const str[] args) {
    try {
        i32 first = before_suspend();
        i32 second = await after_suspend();
        return first + second;
    } catch (EarlyFailure failure) {
        failure as void;
        if (len(args) == 0) {
            return 1;
        }
        return 0;
    } catch (LateFailure failure) {
        failure as void;
        return 2;
    } catch (std.async::start_error failure) {
        failure as void;
        return 3;
    }
}
