module test.regression.len_after_move;

usize moved_length(bytes source) {
    bytes destination = move source;
    usize count = len(source);
    drop destination;
    return count;
}
