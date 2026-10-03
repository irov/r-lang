module test.regression.array_filled_move_element;

// R-LIB-0019 (P4.2): the element of std.array::filled is copied, so a Move element is rejected.
i32 main() {
    try {
        array<u8> empty = std.array::create::<u8>();
        array<array<u8>> rows = std.array::filled(2usize, move empty);
        drop rows;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 0;
}
