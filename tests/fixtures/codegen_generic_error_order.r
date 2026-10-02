module test.codegen.generic_error_order;

/* R-TYPE-0049: a closed instance orders its substituted error set canonically; a named catch
   or plain propagation inside the generic body keeps working whatever order the body saw. */
error Alpha { i32 v; };
error Beta { i32 v; };
error Zed { i32 v; };
error Missing { i32 key; };

@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
i32 handled(F f, i32 x) throws E {
    i32 r = 0;
    try { r += f(x); }
    catch (Beta c) { r += c.v * 10; }
    return r;
}

i32 both(i32 v) throws Alpha, Beta {
    if (v == 1) { throw Alpha {.v = v}; }
    if (v == 2) { throw Beta {.v = v}; }
    return v;
}

i32 lookup(i32 key) throws Zed {
    if (key < 0) { throw Zed {.v = key}; }
    return key + 1;
}

@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
i32 inner(F f, i32 key) throws E, Missing {
    if (key == 99) { throw Missing {.key = key}; }
    return f(key);
}

@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
i32 outer(F f, i32 key) throws E, Missing {
    return inner(move f, key);
}

async i32 pause_value(i32 v) { return v; }

@generic<E: errors & unborrowed & send, F: fn(i32) -> i32 throws(E) & send & unborrowed>
async i32 handled_async(F f, i32 x) throws E, std.async::start_error {
    i32 r = await pause_value(0);
    try { r += f(x); }
    catch (Beta c) { r += c.v * 10; }
    return r;
}

async i32 main() {
    try {
        if (handled(both, 2) != 20) { return 1; }
        if (handled(both, 5) != 5) { return 2; }
        try {
            i32 ignored = handled(both, 1);
            ignored as void;
            return 3;
        } catch (Alpha a) {
            if (a.v != 1) { return 4; }
        }
        if (outer(lookup, 4) != 5) { return 5; }
        try {
            i32 ignored = outer(lookup, -3);
            ignored as void;
            return 6;
        } catch (Zed z) {
            if (z.v != -3) { return 7; }
        } catch (Missing m) {
            m as void;
            return 8;
        }
        try {
            i32 ignored = outer(lookup, 99);
            ignored as void;
            return 9;
        } catch (Zed z) {
            z as void;
            return 10;
        } catch (Missing m) {
            if (m.key != 99) { return 11; }
        }
        if (await handled_async(both, 2) != 20) { return 12; }
        return 0;
    } catch (Alpha a) {
        a as void;
        return 20;
    } catch (Beta b) {
        b as void;
        return 21;
    } catch (Zed z) {
        z as void;
        return 22;
    } catch (Missing m) {
        m as void;
        return 23;
    }
}
