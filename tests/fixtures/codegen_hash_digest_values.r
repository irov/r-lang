module test.codegen.hash_digest_values;

// M20-1: digest structs have their byte field wherever their type is named: a program builds one
// with `{}` or from bytes, reads and writes the field whole or by element, in synchronous and
// asynchronous code, without calling a digest operation first.

protected std.hash::sha256_digest built(u8 seed) {
    std.hash::sha256_digest digest = {};
    for (usize index = 0usize; index < 32usize; index += 1usize) {
        digest.bytes[index] = (seed + (index as u8)) as u8;
    }
    return digest;
}

protected i32 check_sync() {
    std.hash::sha256_digest zero = {};
    u8[32] copied = zero.bytes;
    std.hash::sha256_digest made = built(7u8);
    u8[32] bytes = made.bytes;
    std.hash::sha512_digest wide = {};
    u8[64] filled = {};
    for (usize index = 0usize; index < 64usize; index += 1usize) { filled[index] = 9u8; }
    wide.bytes = filled;
    std.hash::md5_digest small = std.hash::md5_digest {.bytes = {}};
    std.hash::sha1_digest middle = {};
    i32 status = 0;
    if (copied[31] != 0u8 || bytes[0] != 7u8 || bytes[31] != 38u8) { status = 1; }
    if (wide.bytes[63] != 9u8 || small.bytes[15] != 0u8 || middle.bytes[19] != 0u8) { status = 2; }
    return status;
}

protected async i32 pause() { return 0; }

protected async i32 check_async() throws std.async::start_error {
    std.hash::sha256_digest made = built(1u8);
    i32 status = await pause();
    u8[32] bytes = made.bytes;
    std.hash::sha256_digest other = {};
    other.bytes = bytes;
    status += await pause();
    if (other.bytes[31] != 32u8 || bytes[0] != 1u8) { status += 3; }
    return status;
}

async i32 main() {
    try {
        i32 sync_status = check_sync();
        if (sync_status != 0) { return sync_status; }
        i32 async_status = await check_async();
        return async_status;
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
