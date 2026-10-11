module test.codegen.pending_phi_returns;

/* B11-2: a returned value that a short-circuit operator selects (an LLVM phi) passes through a
   finally block, the activation end of @recursion included, and is read again after it. */

protected i32 trace(i32 marker) {
    static i32 total = 0;
    unsafe {
        total += marker;
        return total;
    }
}

protected bool both(bool left, u32 value) {
    try {
        return left && (value == 7u32);
    } finally {
        trace(1) as void;
    }
}

protected bool either(bool left, u32 value) {
    try {
        return left || (value == 7u32);
    } finally {
        trace(10) as void;
    }
}

@recursion(depth = 8)
protected bool descends(u32 remaining, u32 limit) throws core::recursion_error {
    if (remaining == 0u32) {
        return true;
    }
    bool deeper = descends(remaining - 1u32, limit);
    return deeper && (remaining <= limit);
}

i32 main() {
    if ((both(true, 7u32) == false) || (both(true, 8u32) == true) || (both(false, 7u32) == true)) {
        return 1;
    }
    if ((either(false, 7u32) == false) || (either(false, 8u32) == true)) {
        return 2;
    }
    if (trace(0) != 23) {
        return 3;
    }
    try {
        if ((descends(5u32, 5u32) == false) || (descends(5u32, 4u32) == true)) {
            return 4;
        }
    } catch (core::recursion_error refused) {
        return 5;
    }
    return 0;
}
