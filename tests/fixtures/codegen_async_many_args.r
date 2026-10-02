module test.codegen.async_many_args;

protected async i32 sum(i32 first, i32 second, i32 third, i32 fourth, i32 fifth) {
    return first + second + third + fourth + fifth;
}

async i32 main() {
    i32 first = 1;
    i32 second = 2;
    i32 third = 3;
    i32 fourth = 4;
    i32 fifth = 5;
    try {
        task<i32> operation = sum(first, second, third, fourth, fifth);
        i32 value = await move operation;
        return value - 15;
    } catch (std.async::start_error error) {
        error as void;
        return 3;
    }
}
