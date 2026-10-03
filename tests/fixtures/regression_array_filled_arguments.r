module test.regression.array_filled_arguments;

// R-LIB-0019 (P4.2): std.array::filled takes a length and a value.
i32 main() {
    try {
        array<u32> table = std.array::filled(2usize);
        drop table;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
    }
    return 0;
}
