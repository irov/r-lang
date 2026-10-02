module test.codegen.async_checked_effects;

error async_error {
    i32 code;
};

protected async i32 calculate(bool fail) throws async_error {
    if (fail == true) {
        throw {
            .code = 9,
        };
    }
    return 42;
}

async i32 main() {
    try {
        task<i32 throws async_error> success_operation = calculate(false);
        i32 value = await move success_operation;
        if (value != 42) {
            return 1;
        }
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    } catch (async_error error) {
        return 3;
    }

    try {
        task<i32 throws async_error> failure_operation = calculate(true);
        i32 value = await move failure_operation;
        value as void;
        return 4;
    } catch (std.async::start_error error) {
        error as void;
        return 5;
    } catch (async_error error) {
        if (error.code != 9) {
            return 6;
        }
    }
    return 0;
}
