module audit.nll_throw_branch_replaces_borrow;

error StopError {
    i32 code;
};

i32 select(bool stop) throws StopError {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    source += 1;
    if (stop == true) {
        throw StopError {
            .code = 1,
        };
    } else {
        alias = &replacement;
    }
    return *alias - 11;
}

i32 main() {
    try {
        i32 result = select(false);
        return result;
    } catch (StopError error) {
        return error.code;
    }
}
