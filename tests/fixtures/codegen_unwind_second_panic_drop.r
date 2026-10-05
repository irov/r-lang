module test.codegen.unwind_second_panic_drop;

/* L39 (Core R-ERR-0008): a user drop that panics while a panic unwinds is a second panic; the
   program aborts with the report of the first panic and then that of the second. */

protected i32 pick(const u8[] values, usize at) {
    return values[at] as i32;
}

struct Fragile { usize at; };

drop(Fragile* self) {
    u8[2] values = {1u8, 2u8};
    pick(values[0usize..2usize], self->at) as void;
}

i32 main() {
    Fragile fragile = {.at = 5usize};
    panic("first");
}
