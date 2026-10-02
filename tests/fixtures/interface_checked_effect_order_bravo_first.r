module test.interface.checked_effect_order;

error alpha_error {
    i32 code;
};

error bravo_error {
    i32 code;
};

i32 choose(i32 mode) throws bravo_error, alpha_error {
    if (mode == 1) {
        throw alpha_error {
            .code = 11,
        };
    }
    if (mode == 2) {
        throw bravo_error {
            .code = 22,
        };
    }
    return 7;
}
