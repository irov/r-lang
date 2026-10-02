module regression.async_main_scoped_startup_view;

// M22-3: a view of the program arguments that a @scoped call borrows lives in the frame of main;
// the step also declared an unused local copy of it, which the C compiler rejected.

@scoped
async usize measure(str text) {
    return len(text);
}

async i32 main(const str[] arguments) {
    try {
        task_scope(1) scoped {
            usize length = await measure(arguments[0]);
            if (length == 0usize) { return 1; }
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 2;
    }
}
