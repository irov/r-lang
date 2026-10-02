module audit.throw_finally_drops_owner_before_catch;

error Failure {
    i32 code;
};

i32 main() {
    own i32* value = new i32(7);
    try {
        try {
            throw Failure {
                .code = 1,
            };
        } finally {
            drop value;
        }
    } catch (Failure failure) {
        failure.code as void;
    }
    return *value;
}
