module test.codegen.error_families;

import std.string;

/* L21: single inheritance between errors (R-AGG-0011). A catch of a base catches its subtree and
   the nearest ancestor wins regardless of order (R-ERR-0003), a rethrow keeps the concrete type
   (R-ERR-0002), and a value of a base holds any descendant with the fields of the base as its
   prefix, converted implicitly from an exact error or the family of a descendant (R-EXPR-0015). */
i32 drop_count(bool bump) {
    static i32 count = 0;
    unsafe {
        if (bump == true) { count += 1; }
        return count;
    }
}

struct Token { i32 id; };
drop(Token* self) { drop_count(true) as void; }

error io_error { i32 code; };
error net_error : io_error { u16 port; };
error tls_error : net_error { Token session; };
error disk_error : io_error { std.string::string path; };
error base_failure {};
error timeout_failure : base_failure { i32 after; };

void fail(i32 kind) throws io_error, std.alloc::alloc_error {
    throw (kind == 1) io_error {.code = 1};
    throw (kind == 2) net_error {.code = 2, .port = 80u16};
    throw (kind == 3) tls_error {.code = 3, .port = 443u16, .session = Token {.id = 9}};
    throw (kind == 4) disk_error {.code = 4, .path = std.string::from_str("/tmp")};
}

/* The concrete type decides in another function through a rethrow and exact catches. */
i32 triage(io_error failure) throws io_error {
    try {
        throw move failure;
    } catch (tls_error e) {
        return 300 + (e.port as i32);
    } catch (net_error e) {
        return 200 + (e.port as i32);
    } catch (disk_error e) {
        str path = e.path.as_str();
        return 100 + (len(path) as i32);
    }
}

i32 classify(i32 kind) throws std.alloc::alloc_error {
    try {
        fail(kind);
        return 0;
    } catch (net_error e) {
        return e.code * 1000 + (e.port as i32);
    } catch (io_error e) {
        return e.code;
    }
}

/* The same choice with the ancestor written first. */
i32 reversed(i32 kind) throws std.alloc::alloc_error {
    try {
        fail(kind);
        return 0;
    } catch (io_error e) {
        return -e.code;
    } catch (net_error e) {
        return e.code * 1000 + (e.port as i32);
    }
}

i32 relay(i32 kind) throws std.alloc::alloc_error {
    try {
        fail(kind);
        return 0;
    } catch (io_error e) {
        i32 base = e.code;
        try {
            return base * 10000 + triage(move e);
        } catch (io_error rest) {
            return -rest.code;
        }
    }
}

i32 base_code(const io_error* failure) { return failure->code; }

i32 widened(net_error failure) {
    io_error wider = move failure;
    return base_code(&wider);
}

i32 dial(i32 x) throws net_error {
    throw (x < 0) net_error {.code = x, .port = 7u16};
    return x;
}

@generic<E: errors & unborrowed, F: fn(i32) -> i32 throws(E)>
i32 recover(F f, i32 x) throws E {
    try {
        return f(x);
    } catch (io_error failure) {
        return 100 + failure.code;
    }
}

i32 timeout(base_failure failure) {
    try {
        throw move failure;
    } catch (timeout_failure t) {
        return t.after;
    } catch (base_failure b) {
        return 0;
    }
}

i32 main() {
    i32 failures = 0;
    try {
        failures += classify(1) == 1 ? 0 : 1;
        failures += classify(2) == 2080 ? 0 : 2;
        failures += classify(3) == 3443 ? 0 : 4;
        failures += classify(4) == 4 ? 0 : 8;
        failures += drop_count(false) == 1 ? 0 : 16;
        failures += reversed(3) == 3443 && reversed(1) == -1 ? 0 : 32;
        failures += relay(3) == 30743 ? 0 : 64;
        failures += relay(4) == 40104 ? 0 : 128;
        failures += relay(1) == -1 ? 0 : 256;
        failures += drop_count(false) == 3 ? 0 : 512;
    } catch (std.alloc::alloc_error failure) { failures += 1024; }
    failures += widened(net_error {.code = 5, .port = 6u16}) == 5 ? 0 : 2048;
    try {
        failures += recover(dial, -3) == 97 && recover(dial, 4) == 4 ? 0 : 4096;
    } catch (net_error failure) { failures += 4096; }
    failures += timeout(timeout_failure {.after = 9}) == 9 && timeout(base_failure {}) == 0 ? 0 : 8192;
    {
        io_error held = tls_error {.code = 6, .port = 1u16, .session = Token {.id = 2}};
        failures += base_code(&held) == 6 ? 0 : 16384;
    }
    failures += drop_count(false) == 4 ? 0 : 32768;
    return failures;
}
