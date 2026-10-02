module audit.nll_explicit_throw_branches_control;

error StopError {
    i32 code;
};

i32 select(bool stop, bool first) throws StopError {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    source += 1;
    if (stop == true) {
        if (first == true) {
            throw StopError {
                .code = 1,
            };
        } else {
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
        i32 result = select(false, true);
        return result;
    } catch (StopError failure) {
        return failure.code;
    }
}
