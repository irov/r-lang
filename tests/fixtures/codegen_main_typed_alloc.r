module test.codegen.main_typed_alloc;
i32 main() { bytes value = std.bytes::with_capacity(18446744073709551615usize); drop value; return 0; }
