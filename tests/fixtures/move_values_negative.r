module test.ownership.invalid;

protected array<u8> forward(array<u8> value);

protected array<u8> implicit_read(array<u8> value) {
    return value;
}

protected array<u8> double_move(array<u8> value) {
    array<u8> first = forward(move value);
    return move value;
}

protected void self_move(array<u8> value) {
    value = move value;
}

protected array<u8> const_move(const array<u8> value) {
    return move value;
}

protected array<u8> borrow_ends_before_move(array<u8> value) {
    const u8[] view = std.array::as_slice(&value);
    view as void;
    return move value;
}

protected void drop_active_borrow(array<u8> value) {
    const u8[] view = std.array::as_slice(&value);
    drop value;
    view as void;
}
