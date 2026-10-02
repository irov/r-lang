module test.codegen_native_result_abi;

array<u8> pass_fs_array(array<u8> value) throws std.fs::fs_error {
    array<u8> local = move value;
    return move local;
}

std.fs::directory pass_fs_directory(std.fs::directory value) throws std.fs::fs_error {
    std.fs::directory local = move value;
    return move local;
}

void pass_fs_void() throws std.fs::fs_error {
}

void pass_io_void() throws std.io::io_error {
}

i32 main() {
    return 0;
}
