module audit.nll_terminating_switch_branch_replaces_borrow;

error StopError {
    i32 code;
};

i32 select(bool stop, i32 choice) throws StopError {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    source += 1;
    if (stop == true) {
        switch (choice) {
            case 0:
                throw StopError {
                    .code = 1,
                };
            default:
                throw StopError {
                    .code = 2,
                };
        }
    } else {
        alias = &replacement;
    }
    return *alias - 11;
}

i32 main() {
    try {
        i32 result = select(false, 0);
        return result;
    } catch (StopError failure) {
        return failure.code;
    }
}
