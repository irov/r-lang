module audit.nll_throw_branch_finally_uses_old_borrow;

error StopError {
    i32 code;
};

void inspect(bool stop) throws StopError {
    i32 source = 7;
    i32 replacement = 11;
    const i32* alias = &source;
    try {
        source += 1;
        if (stop == true) {
            throw StopError {
                .code = 1,
            };
        } else {
            alias = &replacement;
        }
    } finally {
        i32 observed = *alias;
        observed as void;
    }
}

i32 main() {
    return 0;
}
