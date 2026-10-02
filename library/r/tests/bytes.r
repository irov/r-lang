module tests.std.bytes;
import std.test;
import std.bytes;

// The tests of the R part of std.bytes (Library R-SLIB-BYTES-0009): a cursor that reads and
// writes unsigned integers in little-endian and big-endian order and changes nothing when too few
// bytes follow its position, run in test mode (Core R-FUNC-0025).

@test
void writes_both_byte_orders()
    throws std.bytes::bytes_error, std.test::failure, std.alloc::alloc_error {
    u8[16] buffer = {};
    std.bytes::cursor writer = {};
    writer.write_u8(&buffer, 171u8);
    writer.write_u16_le(&buffer, 4660u16);
    writer.write_u16_be(&buffer, 4660u16);
    writer.write_u32_le(&buffer, 305419896u32);
    writer.write_u32_be(&buffer, 305419896u32);
    std.test::equal(writer.position, 13usize);
    u8[13] expected = {171u8, 52u8, 18u8, 18u8, 52u8, 120u8, 86u8, 52u8, 18u8,
                       18u8, 52u8, 86u8, 120u8};
    std.test::check(std.bytes::equal(buffer[0usize..13usize], expected), "the written bytes");
    std.test::equal(buffer[13], 0u8);
    u8[16] wide = {};
    std.bytes::cursor eight = {};
    eight.write_u64_le(&wide, 72623859790382856u64);
    eight.write_u64_be(&wide, 72623859790382856u64);
    std.test::equal(eight.position, 16usize);
    u8[16] layout = {8u8, 7u8, 6u8, 5u8, 4u8, 3u8, 2u8, 1u8,
                     1u8, 2u8, 3u8, 4u8, 5u8, 6u8, 7u8, 8u8};
    std.test::check(std.bytes::equal(wide, layout), "u64 in both orders");
}

@test
void reads_both_byte_orders()
    throws std.bytes::bytes_error, std.test::failure, std.alloc::alloc_error {
    u8[8] source = {1u8, 2u8, 3u8, 4u8, 5u8, 6u8, 7u8, 8u8};
    std.bytes::cursor big = {};
    std.test::equal(big.read_u64_be(source), 72623859790382856u64);
    std.test::equal(big.position, 8usize);
    std.bytes::cursor little = {};
    std.test::equal(little.read_u64_le(source), 578437695752307201u64);
    std.bytes::cursor mixed = {};
    std.test::equal(mixed.read_u8(source), 1u8);
    std.test::equal(mixed.read_u16_be(source), 515u16);
    std.test::equal(mixed.read_u16_le(source), 1284u16);
    std.test::equal(mixed.position, 5usize);
    std.bytes::cursor words = {};
    std.test::equal(words.read_u32_be(source), 16909060u32);
    std.test::equal(words.read_u32_le(source), 134678021u32);
    std.test::equal(words.position, 8usize);
}

@test
void round_trips_extreme_values()
    throws std.bytes::bytes_error, std.test::failure, std.alloc::alloc_error {
    u8[30] buffer = {};
    std.bytes::cursor writer = {};
    writer.write_u8(&buffer, 255u8);
    writer.write_u16_be(&buffer, 65535u16);
    writer.write_u32_le(&buffer, 4294967295u32);
    writer.write_u64_be(&buffer, 18446744073709551615u64);
    writer.write_u64_le(&buffer, 0u64);
    writer.write_u32_be(&buffer, 1u32);
    writer.write_u16_le(&buffer, 32768u16);
    writer.write_u8(&buffer, 0u8);
    std.test::equal(writer.position, 30usize);
    std.bytes::cursor reader = {};
    std.test::equal(reader.read_u8(buffer), 255u8);
    std.test::equal(reader.read_u16_be(buffer), 65535u16);
    std.test::equal(reader.read_u32_le(buffer), 4294967295u32);
    std.test::equal(reader.read_u64_be(buffer), 18446744073709551615u64);
    std.test::equal(reader.read_u64_le(buffer), 0u64);
    std.test::equal(reader.read_u32_be(buffer), 1u32);
    std.test::equal(reader.read_u16_le(buffer), 32768u16);
    std.test::equal(reader.read_u8(buffer), 0u8);
    std.test::equal(reader.position, 30usize);
}

@test
void refuses_reads_past_the_end()
    throws std.bytes::bytes_error, std.test::failure, std.alloc::alloc_error {
    u8[3] source = {1u8, 2u8, 3u8};
    std.bytes::cursor reader = std.bytes::cursor {.position = 1usize};
    try {
        u32 value = reader.read_u32_le(source);
        value as void;
        std.test::fail("four bytes do not follow position 1 of three");
    } catch (std.bytes::bytes_error failure) {
        std.test::check(failure == std.bytes::bytes_error::out_of_bounds, "out_of_bounds");
    }
    std.test::equal(reader.position, 1usize);
    std.test::equal(reader.read_u16_be(source), 515u16);
    std.test::equal(reader.position, 3usize);
    try {
        u8 beyond = reader.read_u8(source);
        beyond as void;
        std.test::fail("no byte follows the end");
    } catch (std.bytes::bytes_error failure) {
        std.test::check(failure == std.bytes::bytes_error::out_of_bounds, "at the end");
    }
    std.test::equal(reader.position, 3usize);
    std.bytes::cursor late = std.bytes::cursor {.position = 9usize};
    try {
        u16 value = late.read_u16_le(source);
        value as void;
        std.test::fail("a position beyond the length");
    } catch (std.bytes::bytes_error failure) {
        std.test::check(failure == std.bytes::bytes_error::out_of_bounds, "beyond the length");
    }
    std.test::equal(late.position, 9usize);
}

@test(expect = std.bytes::bytes_error)
void an_empty_source_has_no_byte() throws std.bytes::bytes_error {
    u8[1] single = {5u8};
    std.bytes::cursor reader = {};
    u8 value = reader.read_u8(single[0usize..0usize]);
    value as void;
}

@test
void refuses_writes_that_do_not_fit()
    throws std.bytes::bytes_error, std.test::failure, std.alloc::alloc_error {
    u8[6] target = {9u8, 9u8, 9u8, 9u8, 9u8, 9u8};
    std.bytes::cursor writer = std.bytes::cursor {.position = 3usize};
    try {
        writer.write_u32_be(&target, 16909060u32);
        std.test::fail("four bytes do not fit after position 3 of six");
    } catch (std.bytes::bytes_error failure) {
        std.test::check(failure == std.bytes::bytes_error::out_of_bounds, "out_of_bounds");
    }
    std.test::equal(writer.position, 3usize);
    u8[6] untouched = {9u8, 9u8, 9u8, 9u8, 9u8, 9u8};
    std.test::check(std.bytes::equal(target, untouched), "a failed write changes no byte");
    writer.write_u16_be(&target, 258u16);
    writer.write_u8(&target, 3u8);
    std.test::equal(writer.position, 6usize);
    u8[6] filled = {9u8, 9u8, 9u8, 1u8, 2u8, 3u8};
    std.test::check(std.bytes::equal(target, filled), "the writes that fit");
    try {
        writer.write_u8(&target, 4u8);
        std.test::fail("the target is full");
    } catch (std.bytes::bytes_error failure) {
        std.test::check(failure == std.bytes::bytes_error::out_of_bounds, "a full target");
    }
    std.test::equal(writer.position, 6usize);
}

@test(allocations)
void round_trips_an_owned_record()
    throws std.bytes::bytes_error, std.alloc::alloc_error, std.test::failure {
    bytes record = std.bytes::with_capacity(4usize);
    std.bytes::append_u8(&record, 7u8);
    std.bytes::append_u16_le(&record, 513u16);
    std.bytes::append_u32_le(&record, 3735928559u32);
    std.bytes::append_u64_le(&record, 72623859790382856u64);
    std.test::equal(len(record), 15usize);
    std.bytes::cursor reader = {};
    std.test::equal(reader.read_u8(record.as_slice()), 7u8);
    std.test::equal(reader.read_u16_le(record.as_slice()), 513u16);
    std.test::equal(reader.read_u32_le(record.as_slice()), 3735928559u32);
    std.test::equal(reader.read_u64_le(record.as_slice()), 72623859790382856u64);
    std.test::equal(reader.position, 15usize);
    bytes frame = std.alloc::bytes(6usize, 0u8);
    std.bytes::cursor writer = {};
    writer.write_u16_be(frame.as_slice_mut(), 51966u16);
    writer.write_u32_be(frame.as_slice_mut(), 3735928559u32);
    u8[6] expected = {202u8, 254u8, 222u8, 173u8, 190u8, 239u8};
    std.test::check(std.bytes::equal(frame.as_slice(), expected), "a big-endian frame");
    std.bytes::cursor back = {};
    std.test::equal(back.read_u16_le(frame.as_slice()), 65226u16);
    std.test::equal(back.read_u32_le(frame.as_slice()), 4022250974u32);
}
