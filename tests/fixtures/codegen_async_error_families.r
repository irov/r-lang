module test.codegen.async_error_families;

import std.string;

/* L21 in asynchronous frames: family catches after awaits, a rethrow of a base value that keeps
   the concrete type, widening from a descendant family, task groups whose members throw
   descendants, and cleanup of held members (R-AGG-0011, R-ERR-0002, R-ERR-0003). */
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

async void fail(i32 kind) throws io_error, std.alloc::alloc_error {
    throw (kind == 1) io_error {.code = 1};
    throw (kind == 2) net_error {.code = 2, .port = 80u16};
    throw (kind == 3) tls_error {.code = 3, .port = 443u16, .session = Token {.id = 9}};
    throw (kind == 4) disk_error {.code = 4, .path = std.string::from_str("/tmp")};
}

/* The concrete type decides after a rethrow in another function. */
async i32 triage(io_error failure) throws io_error {
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

i32 base_code(io_error failure) { return failure.code; }

async i32 classify(i32 kind) throws std.alloc::alloc_error, std.async::start_error {
    try {
        await fail(kind);
        return 0;
    } catch (net_error e) {
        /* A net_error family widens to the io_error family of the parameter. */
        return base_code(move e) * 1000 + 7;
    } catch (io_error e) {
        return e.code;
    }
}

async i32 relay(i32 kind) throws std.alloc::alloc_error, std.async::start_error {
    try {
        await fail(kind);
        return 0;
    } catch (io_error e) {
        i32 base = e.code;
        try {
            return base * 10000 + await triage(move e);
        } catch (io_error rest) {
            return -rest.code;
        }
    }
}

async i32 fetch(i32 kind) throws io_error {
    throw (kind == 1) net_error {.code = 10, .port = 80u16};
    throw (kind == 2) io_error {.code = 20};
    return kind;
}

async i32 gather(i32 kind) throws std.async::start_error, io_error {
    i32 total = 0;
    task_scope(2) group {
        auto first = fetch(kind);
        auto second = fetch(0);
        select (group) {
        case i32 value = await move first:
            total = value + await move second;
        case i32 value = await move second:
            total = value + await move first;
        }
    }
    return total;
}

async i32 main() {
    i32 failures = 0;
    try {
        failures += await classify(1) == 1 ? 0 : 1;
        failures += await classify(2) == 2007 ? 0 : 2;
        failures += await classify(3) == 3007 ? 0 : 4;
        failures += await classify(4) == 4 ? 0 : 8;
        failures += drop_count(false) == 1 ? 0 : 16;
        failures += await relay(3) == 30743 ? 0 : 32;
        failures += await relay(4) == 40104 ? 0 : 64;
        failures += await relay(1) == -1 ? 0 : 128;
        failures += drop_count(false) == 2 ? 0 : 256;
    } catch (std.alloc::alloc_error failure) { failures += 512; }
    catch (std.async::start_error failure) { failures += 1024; }
    try {
        failures += await gather(5) == 5 ? 0 : 2048;
        i32 lost = await gather(1);
        failures += lost >= 0 ? 4096 : 4096;
    } catch (net_error e) {
        failures += e.port == 80u16 ? 0 : 8192;
    } catch (io_error e) {
        failures += 16384;
    } catch (std.async::start_error e) { failures += 32768; }
    try {
        i32 lost = await gather(2);
        failures += lost >= 0 ? 65536 : 65536;
    } catch (io_error e) {
        failures += e.code == 20 ? 0 : 131072;
    } catch (std.async::start_error e) { failures += 262144; }
    return failures;
}
