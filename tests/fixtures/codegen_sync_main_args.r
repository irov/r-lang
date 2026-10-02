module test.codegen.sync_main_args;

i32 main(const str[] args) {
    usize argument_count = len(args);
    if (argument_count != 2) {
        return 91;
    }
    str program = args[0];
    usize program_length = len(program);
    if (program_length == 0) {
        return 92;
    }
    str argument = args[1];
    usize argument_length = len(argument);
    if (argument_length != 8) {
        return 93;
    }
    return 37;
}
