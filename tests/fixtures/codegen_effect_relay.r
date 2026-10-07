module test.codegen.effect_relay;

import std.string;

/* L25.4: the errors of one call that share their cleanup leave through one relay. Every error the
   caller does not catch moves with the callee's carrier into the caller's carrier, whose tags
   differ here, and the members of one family that the same catch receives move into its family
   value; an exact catch keeps its own path. Payloads that own storage and a user drop show that
   each error is moved once and dropped once, also across a finally and a task group
   (R-ERR-0001, R-ERR-0002, R-ERR-0003). */
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
error quota_error { u32 limit; };

void fail(i32 kind) throws io_error, std.convert::parse_error, std.alloc::alloc_error {
    throw (kind == 1) io_error {.code = 1};
    throw (kind == 2) net_error {.code = 2, .port = 80u16};
    throw (kind == 3) tls_error {.code = 3, .port = 443u16, .session = Token {.id = 9}};
    throw (kind == 4) disk_error {.code = 4, .path = std.string::from_str("/tmp/relay")};
    if (kind == 5) { std.convert::parse_i32("?", 10u32) as void; }
}

/* quota_error sorts before the members of io_error, so every relayed tag changes. */
usize forward(i32 kind) throws quota_error, io_error, std.convert::parse_error,
                              std.alloc::alloc_error {
    std.string::string held = std.string::from_str("held");
    throw (kind == 9) quota_error {.limit = 7u32};
    fail(kind);
    held.append("!");
    const str text = held;
    return len(text);
}

i32 family_code(io_error failure) {
    try {
        throw move failure;
    } catch (net_error e) {
        return 200 + (e.port as i32);
    } catch (disk_error e) {
        str path = e.path;
        return 100 + (len(path) as i32);
    } catch (io_error e) {
        return e.code;
    }
}

/* Three family members go to one catch, the rest of the set to the caller: two relays. */
i32 classify(i32 kind) throws quota_error, std.convert::parse_error, std.alloc::alloc_error {
    try {
        std.string::string held = std.string::from_str("classify");
        usize length = forward(kind);
        const str text = held;
        return (length + len(text)) as i32;
    } catch (tls_error e) {
        return 300 + e.session.id;
    } catch (io_error e) {
        return family_code(move e);
    }
}

/* The standard errors of classify reach one catch of the root through one relay. */
i32 fault_domain(i32 kind) throws quota_error {
    try {
        i32 code = classify(kind);
        return code;
    } catch (std.error::fault failure) {
        std.error::error portable = std.error::from_fault(failure);
        if (portable.domain == std.error::domain::conversion) { return -1; }
        return -2;
    }
}

i32 check_sync() {
    i32 failures = 0;
    try {
        failures += classify(0) == 13 ? 0 : 1;
        failures += classify(1) == 1 ? 0 : 2;
        failures += classify(2) == 280 ? 0 : 4;
        failures += classify(3) == 309 ? 0 : 8;
        failures += drop_count(false) == 1 ? 0 : 16;
        failures += classify(4) == 110 ? 0 : 32;
        i32 lost = classify(5);
        failures += lost >= 0 ? 64 : 64;
    } catch (std.convert::parse_error e) {
        failures += 0;
    } catch (quota_error e) {
        failures += 128;
    } catch (std.alloc::alloc_error e) {
        failures += 256;
    }
    try {
        i32 lost = classify(9);
        failures += lost >= 0 ? 512 : 512;
    } catch (quota_error e) {
        failures += e.limit == 7u32 ? 0 : 1024;
    } catch (std.convert::parse_error e) {
        failures += 2048;
    } catch (std.alloc::alloc_error e) {
        failures += 4096;
    }
    try {
        failures += fault_domain(5) == -1 ? 0 : 4096;
        failures += fault_domain(2) == 280 ? 0 : 4096;
    } catch (quota_error e) {
        failures += 4096;
    }
    return failures;
}

async void afail(i32 kind) throws io_error, std.convert::parse_error, std.alloc::alloc_error {
    throw (kind == 1) io_error {.code = 1};
    throw (kind == 2) net_error {.code = 2, .port = 80u16};
    throw (kind == 3) tls_error {.code = 3, .port = 443u16, .session = Token {.id = 9}};
    throw (kind == 4) disk_error {.code = 4, .path = std.string::from_str("/tmp/relay")};
    if (kind == 5) { std.convert::parse_i32("?", 10u32) as void; }
}

/* The group's drain is a finally between the await and the caller. */
async usize aforward(i32 kind) throws quota_error, io_error, std.convert::parse_error,
                                      std.alloc::alloc_error, std.async::start_error {
    std.string::string held = std.string::from_str("held");
    throw (kind == 9) quota_error {.limit = 7u32};
    task_scope(1) group {
        await afail(kind);
    }
    held.append("!");
    const str text = held;
    return len(text);
}

/* The carrier is the pending error while the inner finally runs, then the catch receives it. */
async i32 aclassify(i32 kind) throws quota_error, std.convert::parse_error,
                                     std.alloc::alloc_error, std.async::start_error {
    try {
        try {
            std.string::string held = std.string::from_str("classify");
            usize length = await aforward(kind);
            const str text = held;
            return (length + len(text)) as i32;
        } finally {
            drop_count(false) as void;
        }
    } catch (tls_error e) {
        return 300 + e.session.id;
    } catch (io_error e) {
        return family_code(move e);
    }
}

async i32 afault_domain(i32 kind) throws quota_error {
    try {
        i32 code = await aclassify(kind);
        return code;
    } catch (std.error::fault failure) {
        std.error::error portable = std.error::from_fault(failure);
        if (portable.domain == std.error::domain::conversion) { return -1; }
        return -2;
    }
}

async i32 main() {
    i32 failures = check_sync();
    try {
        failures += await aclassify(0) == 13 ? 0 : 8192;
        failures += await aclassify(1) == 1 ? 0 : 16384;
        failures += await aclassify(2) == 280 ? 0 : 32768;
        failures += await aclassify(3) == 309 ? 0 : 65536;
        failures += drop_count(false) == 2 ? 0 : 131072;
        failures += await aclassify(4) == 110 ? 0 : 262144;
        i32 lost = await aclassify(5);
        failures += lost >= 0 ? 524288 : 524288;
    } catch (std.convert::parse_error e) {
        failures += 0;
    } catch (quota_error e) {
        failures += 1048576;
    } catch (std.alloc::alloc_error e) {
        failures += 2097152;
    } catch (std.async::start_error e) {
        failures += 4194304;
    }
    try {
        i32 lost = await aclassify(9);
        failures += lost >= 0 ? 8388608 : 8388608;
    } catch (quota_error e) {
        failures += e.limit == 7u32 ? 0 : 16777216;
    } catch (std.error::fault e) {
        failures += 33554432;
    }
    try {
        failures += await afault_domain(5) == -1 ? 0 : 67108864;
        failures += await afault_domain(3) == 309 ? 0 : 67108864;
    } catch (quota_error e) {
        failures += 67108864;
    }
    return failures;
}
