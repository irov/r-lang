module test.semantic.throw_ambiguous_aggregate;

error first_error {
    i32 code;
};

error second_error {
    i32 code;
};

void fail() throws first_error, second_error {
    throw {
        .code = 1,
    };
}
