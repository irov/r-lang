module test.ownership.values;

error move_error {
    i32 code;
};

protected array<u8> forward(array<u8> value) {
    return move value;
}

protected void dispose(array<u8> value) {
}

protected array<u8> allocate(usize capacity) throws move_error {
    try {
        array<u8> bytes = std.array::with_capacity::<u8>(capacity);
        return move bytes;
    } catch (std.alloc::alloc_error error) {
        error as void;
        throw {.code = 1};
    }
}

protected std.fs::path path(str text) throws move_error {
    try {
        std.fs::path value = std.fs::path_from_utf8(text);
        return move value;
    } catch (std.fs::path_error error) {
        error as void;
        throw {.code = 2};
    }
}

protected i32 copy_move(const i32 value) {
    i32 first = move value;
    i32 second = move value;
    return first + second + value;
}
