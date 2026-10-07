module test.codegen.hash_driver;

import std.console;
import std.encoding;
import std.hash;
import std.random;

// The arguments are triples of a mode, a key and a text; the program prints one line per
// triple (M20 differential test, tests/hash_differential.py). sha256 and sha512 hash the text in
// pieces of seven bytes, hmac256 and hmac512 sign the text with the key, and generator draws
// from a generator seeded with the decimal key.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected std.string::string digest(str mode, str key, str text) throws std.alloc::alloc_error {
    const u8[] bytes = text;
    usize total = len(bytes);
    if (same(mode, "sha256") == true) {
        std.hash::sha256_state state = std.hash::sha256_state::create();
        for (usize at = 0usize; at < total; at += 7usize) {
            usize end = at + 7usize;
            if (end > total) { end = total; }
            state.update(bytes[at..end]);
        }
        std.hash::sha256_digest result = state.finish();
        return std.encoding::encode_hex(result.bytes);
    }
    if (same(mode, "sha512") == true) {
        std.hash::sha512_state state = std.hash::sha512_state::create();
        for (usize at = 0usize; at < total; at += 7usize) {
            usize end = at + 7usize;
            if (end > total) { end = total; }
            state.update(bytes[at..end]);
        }
        std.hash::sha512_digest result = state.finish();
        return std.encoding::encode_hex(result.bytes);
    }
    if (same(mode, "hmac256") == true) {
        std.hash::sha256_digest result = std.hash::hmac_sha256(key, text);
        return std.encoding::encode_hex(result.bytes);
    }
    std.hash::sha512_digest result = std.hash::hmac_sha512(key, text);
    return std.encoding::encode_hex(result.bytes);
}

protected std.string::string draws(u64 seed) throws std.alloc::alloc_error {
    std.random::generator values = std.random::generator::seeded(seed);
    u64 word = values.next_u64();
    u32 half = values.next_u32();
    u64 tenth = values.below(10u64);
    u64 ranged = values.range(100u64, 200u64);
    u8[5] noise = {};
    values.fill(&noise);
    std.string::string hex = std.encoding::encode_hex(noise);
    i32[8] cards = {0, 1, 2, 3, 4, 5, 6, 7};
    values.shuffle(&cards);
    std.string::string out = f"{word} {half} {tenth} {ranged} {hex}";
    for (usize index = 0usize; index < 8usize; index += 1usize) {
        i32 card = cards[index];
        std.string::string item = f" {card}";
        std.string::append_str(&out, item);
    }
    return move out;
}

async i32 main(const str[] arguments) {
    try {
        std.string::string out = std.string::create();
        for (usize at = 1usize; at + 2usize < len(arguments); at += 3usize) {
            str mode = arguments[at];
            if (same(mode, "generator") == true) {
                u64 seed = std.convert::parse_u64(arguments[at + 1usize], 10u32);
                std.string::string line = draws(seed);
                std.string::append_str(&out, line);
            } else {
                std.string::string line = digest(mode, arguments[at + 1usize], arguments[at + 2usize]);
                std.string::append_str(&out, line);
            }
            std.string::append_str(&out, "\n");
        }
        await std.console::print(move out);
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 98;
    } catch (std.error::fault failure) {
        failure as void;
        return 97;
    }
}
