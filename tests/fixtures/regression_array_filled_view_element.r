module test.regression.array_filled_view_element;

// R-LIB-0019 (P4.2): a Copy element that holds a view is rejected; the copies would need regions.
i32 main() {
    try {
        array<str> words = std.array::filled(2usize, "word");
        drop words;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 0;
}
