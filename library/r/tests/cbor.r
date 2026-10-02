module tests.std.cbor;
import std.test;
import std.cbor;

// The tests of std.cbor (Library R-SLIB-CBOR-0001..0006), run in test mode (Core R-FUNC-0025).

protected std.string::string shown(const u8[] input)
    throws std.test::failure, std.alloc::alloc_error, std.cbor::cbor_error {
    std.cbor::value item = std.cbor::decode(input);
    return std.cbor::diagnostic(&item);
}

/* Fails unless decoding reports the code. */
protected void expect_error(const u8[] input, std.cbor::error_code code, bool strict)
    throws std.test::failure, std.alloc::alloc_error {
    try {
        if (strict == true) {
            std.cbor::value item = std.cbor::decode_deterministic(input);
            drop item;
        } else {
            std.cbor::value item = std.cbor::decode(input);
            drop item;
        }
        std.test::fail("decoded");
    } catch (std.cbor::cbor_error failure) {
        std.test::check(failure.code == code, "the error code");
    }
}

@test
void decodes_and_shows_items() throws std.test::failure, std.alloc::alloc_error, std.cbor::cbor_error {
    u8[9] nested = {0x83u8, 0x01u8, 0x82u8, 0x02u8, 0x03u8, 0xa1u8, 0x61u8, 0x61u8, 0xf5u8};
    std.string::string text = shown(nested[..]);
    std.test::equal_text(text.as_str(), "[1, [2, 3], {\"a\": true}]");
    u8[3] half = {0xf9u8, 0x3eu8, 0x00u8};
    std.string::string float_text = shown(half[..]);
    std.test::equal_text(float_text.as_str(), "1.5");
    u8[6] tagged = {0xc1u8, 0x1au8, 0x51u8, 0x4bu8, 0x67u8, 0xb0u8};
    std.string::string tag_text = shown(tagged[..]);
    std.test::equal_text(tag_text.as_str(), "1(1363896240)");
}

@test
void encodes_deterministically() throws std.test::failure, std.alloc::alloc_error, std.cbor::cbor_error {
    // {"b": 1, "a": 2} written in another order comes out with the shorter key first.
    array<std.cbor::entry> entries = [];
    try {
        entries.push(std.cbor::entry {.key = std.cbor::value::of_text("b"), .item = std.cbor::value::integer(1i64)});
        entries.push(std.cbor::entry {.key = std.cbor::value::of_text("a"), .item = std.cbor::value::integer(-2i64)});
    } catch (std.array::push_error<std.cbor::entry> failure) {
        drop failure;
        std.test::fail("push");
    }
    std.cbor::value map = std.cbor::value::map(move entries);
    bytes encoded = std.cbor::encode(&map);
    u8[7] expected = {0xa2u8, 0x61u8, 0x61u8, 0x21u8, 0x61u8, 0x62u8, 0x01u8};
    std.test::check(std.bytes::equal(encoded.as_slice(), expected[..]), "ordered map");
    std.cbor::value again = std.cbor::decode_deterministic(encoded.as_slice());
    drop again;
}

@test
void streams_items() throws std.test::failure, std.alloc::alloc_error, std.cbor::cbor_error {
    std.cbor::encoder writer = std.cbor::encoder::create();
    writer.begin_array(3u64);
    writer.integer(-1000i64);
    writer.float(100000.0);
    writer.tag(24u64);
    writer.bytes("ok");
    std.test::equal(writer.length(), 14usize);
    bytes out = writer.finish();
    std.string::string text = shown(out.as_slice());
    std.test::equal_text(text.as_str(), "[-1000, 1e5, 24(h'6f6b')]");
    std.test::equal(writer.length(), 0usize);
}

@test
void refuses_bad_input() throws std.test::failure, std.alloc::alloc_error {
    u8[1] reserved = {0x1cu8};
    expect_error(reserved[..], std.cbor::error_code::malformed, false);
    u8[2] cut = {0x82u8, 0x01u8};
    expect_error(cut[..], std.cbor::error_code::truncated, false);
    u8[2] extra = {0x00u8, 0x00u8};
    expect_error(extra[..], std.cbor::error_code::trailing_data, false);
    u8[3] latin = {0x62u8, 0xc3u8, 0x28u8};
    expect_error(latin[..], std.cbor::error_code::invalid_utf8, false);
    u8[5] twice = {0xa2u8, 0x01u8, 0x02u8, 0x01u8, 0x03u8};
    expect_error(twice[..], std.cbor::error_code::duplicate_key, false);
    u8[2] long_head = {0x18u8, 0x17u8};
    expect_error(long_head[..], std.cbor::error_code::not_deterministic, true);
}
