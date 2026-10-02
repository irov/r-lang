module test.async_copy_args;

protected async i32 encode(i32 hundreds, i32 tens, i32 ones) {
    return hundreds * 100 + tens * 10 + ones;
}

async i32 main() {
    i32 hundreds = 1;
    i32 tens = 2;
    i32 ones = 3;
    try {
        task<i32> operation = encode(hundreds, tens, ones);
        i32 preserved = hundreds * 100 + tens * 10 + ones;
        if (preserved != 123) {
            std.async::cancel(move operation);
            return 4;
        }
        i32 value = await move operation;
        return value - 123;
    } catch (std.async::start_error error) {
        error as void;
        return 3;
    }
}
