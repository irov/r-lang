module codegen.sanitized_heap_overflow;

/* B8.3: under the address sanitizer the R code of a program is instrumented. An unsafe view that
   claims more elements than its heap storage holds lets an index past the storage through its
   bounds check, and the sanitizer reports the read (heap-buffer-overflow). The test runs in the
   sanitizers preset only. */
i32 main() {
    array<u32> values = std.array::filled(4usize, 7u32);
    u32 read = 0u32;
    {
        const u32[] stored = std.array::as_slice(&values);
        unsafe {
            raw const u32* first = &stored[0] as raw const u32*;
            const u32[] view = core::slice_from_raw_parts(first, 64usize);
            read += view[40];
        }
    }
    drop values;
    return (read % 2u32) as i32;
}
