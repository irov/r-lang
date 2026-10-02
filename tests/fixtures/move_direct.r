module test.ownership.direct;

protected array<u8> forward(array<u8> value) {
    return move value;
}

protected void dispose(array<u8> value) {
    return;
}
