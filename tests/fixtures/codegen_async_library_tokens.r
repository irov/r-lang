module test.codegen.async_library_tokens;

import std.hash;
import std.random;
import std.uuid;

// M20 in async functions: identifiers, digest states and generators are values that stay valid
// across awaits.

protected async i32 pause() { return 0; }

protected async i32 identifiers() throws std.async::start_error, std.time::time_error {
    std.uuid::uuid first = std.uuid::v7();
    i32 status = await pause();
    std.uuid::uuid second = std.uuid::v4();
    u8 first_version = std.uuid::version(&first);
    u8 second_version = std.uuid::version(&second);
    if (first_version != 7u8) { status += 1; }
    if (second_version != 4u8) { status += 1; }
    return status;
}

protected async i32 digests() throws std.async::start_error {
    std.hash::sha256_state state = std.hash::sha256_state::create();
    state.update("hello ");
    i32 status = await pause();
    state.update("world");
    std.hash::sha256_digest pieces = state.finish();
    std.hash::sha256_digest whole = std.hash::sha256("hello world");
    if (std.bytes::equal(pieces.bytes, whole.bytes) == false) { status += 1; }
    std.hash::sha512_digest code = std.hash::hmac_sha512("key", "message");
    status += await pause();
    std.hash::sha512_digest again = std.hash::hmac_sha512("key", "message");
    if (std.secret::constant_time_equal(code.bytes, again.bytes) == false) { status += 2; }
    return status;
}

protected async i32 draws() throws std.async::start_error {
    std.random::generator dice = std.random::generator::seeded(42u64);
    u64 first = dice.next_u64();
    i32 status = await pause();
    u64 second = dice.next_u64();
    if (first != 1546998764402558742u64) { status += 1; }
    if (second != 6990951692964543102u64) { status += 1; }
    u8[16] noise = {};
    std.random::fill(&noise);
    status += await pause();
    u64 bounded = std.random::below(7u64);
    if (bounded >= 7u64) { status += 2; }
    return status;
}

async i32 main() {
    try {
        i32 first = await identifiers();
        if (first != 0) { return 1; }
        i32 second = await digests();
        if (second != 0) { return 2; }
        i32 third = await draws();
        if (third != 0) { return 3; }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    } catch (std.time::time_error failure) {
        failure as void;
        return 91;
    }
}
