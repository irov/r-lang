module test.codegen.index_proofs_static_bounds;

/* P4.4-2, R-EXPR-0021 with index proofs: a call may change a static local, so a condition on
   one proves nothing about an index after the call. The recursive call moves the position past
   the end, the check stays, and the read panics. */

@recursion(depth = 2)
u32 visit(const u32[] data, bool outer) throws core::recursion_error {
    static usize at = 0usize;
    u32 total = 0u32;
    unsafe {
        if (outer == false) {
            at = 1000usize;
            return 0u32;
        }
        if (at == 0usize) { at = 1usize; }
        if (at < len(data)) {
            u32 ignored = visit(data, false);
            ignored as void;
            total += data[at]; /* checked */
        }
    }
    return total;
}

i32 main() {
    u32[4] storage = {1u32, 2u32, 3u32, 4u32};
    try {
        return visit(storage[..], true) as i32;
    } catch (core::recursion_error failure) {
        failure as void;
        return 1;
    }
}
