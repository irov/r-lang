module test.codegen.bytes_operations;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct ByteScratch {
    u8[5] bytes;
};

protected i32 sync_checks() {
    constexpr str digits = "123456789";
    str text = digits;
    const u8[] source = text;
    bytes value = {};

    try {
        std.bytes::append(&value, source);

        u8[2] fixed = {0x6f, 0x6b};
        u8[] mutable_source = &fixed;
        array<u8> plain_array = {};
        bool valid_slice = std.utf8::is_valid(source);
        bool valid_mutable = std.utf8::is_valid(mutable_source);
        valid_mutable as void;
        bool valid_fixed = std.utf8::is_valid(fixed);
        valid_fixed as void;
        bool valid_text = std.utf8::is_valid(text);
        valid_text as void;
        bool valid_constexpr = std.utf8::is_valid(digits);
        valid_constexpr as void;
        bool valid_bytes = std.utf8::is_valid(value);
        valid_bytes as void;
        bool valid_array = std.utf8::is_valid(plain_array);
        valid_array as void;
        if ((valid_slice != true) || (valid_mutable != true) || (valid_fixed != true) ||
            (valid_text != true) || (valid_constexpr != true) || (valid_bytes != true) ||
            (valid_array != true)) {
            return 2;
        }

        constexpr str sample_text = "abcde";
        u8[5] scratch_storage = {};
        u8[] scratch = &scratch_storage;
        usize copied = std.bytes::copy(scratch, sample_text);
        if ((copied != 5usize) || (scratch[0] != 0x61) || (scratch[4] != 0x65)) {
            return 41;
        }
        usize shifted = std.bytes::copy_within(scratch, 1usize, 0usize, 4usize);
        if ((shifted != 4usize) || (scratch[0] != 0x61) || (scratch[1] != 0x61) ||
            (scratch[4] != 0x64)) {
            return 42;
        }
        std.bytes::fill(scratch, 0x7a);
        if ((scratch[0] != 0x7a) || (scratch[4] != 0x7a)) {
            return 43;
        }
        o<usize> found = std.bytes::find(sample_text, 0x63);
        switch (found) {
            case variant o::some(index): if (*index != 2usize) { return 44; } break;
            case variant o::none: return 45;
        }
        constexpr str needle_text = "cd";
        o<usize> found_slice = std.bytes::find_slice(sample_text, needle_text);
        switch (found_slice) {
            case variant o::some(index): if (*index != 2usize) { return 46; } break;
            case variant o::none: return 47;
        }
        if ((std.bytes::starts_with(sample_text, "ab") == false) ||
            (std.bytes::ends_with(sample_text, "de") == false)) {
            return 48;
        }

        str validated_slice = std.utf8::validate(source);
        str validated_mutable = std.utf8::validate(mutable_source);
        str validated_fixed = std.utf8::validate(fixed);
        str validated_text = std.utf8::validate(text);
        str validated_constexpr = std.utf8::validate(digits);
        str validated_bytes = std.utf8::validate(value);
        str validated_array = std.utf8::validate(plain_array);
        str validated_core = core::validate_utf8(source);
        validated_slice as void;
        validated_mutable as void;
        validated_fixed as void;
        validated_text as void;
        validated_constexpr as void;
        validated_bytes as void;
        validated_array as void;
        validated_core as void;

        u32 checksum = std.hash::crc32(source);
        if (checksum != 0xcbf4_3926) {
            return 4;
        }
        std.hash::md5_digest md5 = std.hash::md5(source);
        if ((md5.bytes[0] != 0x25) || (md5.bytes[15] != 0x0b)) {
            return 5;
        }
        std.hash::sha1_digest sha1 = std.hash::sha1(source);
        if ((sha1.bytes[0] != 0xf7) || (sha1.bytes[19] != 0x41)) {
            return 6;
        }
        std.hash::sha256_digest sha256 = std.hash::sha256(source);
        if ((sha256.bytes[0] != 0x15) || (sha256.bytes[31] != 0x25)) {
            return 7;
        }
        std.hash::sha512_digest sha512 = std.hash::sha512(source);
        if ((sha512.bytes[0] != 0xd9) || (sha512.bytes[63] != 0x85)) {
            return 8;
        }

        u8[9] bit_source = {
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        };
        std.bits::lsb_reader reader = {};
        u64 first = std.bits::read(bit_source, &reader, 1);
        if (first != 1) {
            return 9;
        }
        u64 wide = std.bits::read(bit_source, &reader, 64);
        if (wide != 0xffff_ffff_ffff_ffff) {
            return 11;
        }
        std.bits::align_byte(&reader);
        if ((reader.byte_index != 9) || (reader.bit_count != 0)) {
            return 13;
        }

        std.bytes::append_u8(&value, 0xaa);
        std.bytes::append_u16_le(&value, 0x1122);
        std.bytes::append_u32_le(&value, 0x3344_5566);
        std.bytes::append_u64_le(&value, 0x7788_99aa_bbcc_ddee);
        bytes reserved = std.bytes::with_capacity(16);
        test_observe(&reserved);
        return 0;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 14;
    } catch (core::utf8_error error) {
        error as void;
        return 3;
    } catch (std.bits::read_error error) {
        error as void;
        return 12;
    } catch (std.bytes::bytes_error error) {
        error as void;
        return 49;
    }
}

protected async i32 async_checks() {
    constexpr str digits = "123456789";
    str text = digits;
    const u8[] source = text;
    bytes value = {};

    try {
        std.bytes::append(&value, source);
        bool valid = std.utf8::is_valid(source);
        if (valid != true) {
            return 22;
        }
        str validated = std.utf8::validate(source);
        validated as void;

        constexpr str sample_text = "abcde";
        own ByteScratch* scratch_owner = new ByteScratch{};
        {
            u8[] scratch = scratch_owner->bytes[0..5];
            usize copied = std.bytes::copy(scratch, sample_text);
            if ((copied != 5usize) || (scratch[0] != 0x61) || (scratch[4] != 0x65)) {
                return 51;
            }
            usize shifted = std.bytes::copy_within(scratch, 1usize, 0usize, 4usize);
            if ((shifted != 4usize) || (scratch[1] != 0x61) || (scratch[4] != 0x64)) {
                return 52;
            }
            std.bytes::fill(scratch, 0x79);
            if ((scratch[0] != 0x79) || (scratch[4] != 0x79)) {
                return 53;
            }
        }
        o<usize> found = std.bytes::find(sample_text, 0x64);
        switch (found) {
            case variant o::some(index): if (*index != 3usize) { return 54; } break;
            case variant o::none: return 55;
        }
        o<usize> found_slice = std.bytes::find_slice(sample_text, "bc");
        switch (found_slice) {
            case variant o::some(index): if (*index != 1usize) { return 56; } break;
            case variant o::none: return 57;
        }
        if ((std.bytes::starts_with(sample_text, "abc") == false) ||
            (std.bytes::ends_with(sample_text, "cde") == false)) {
            return 58;
        }

        u32 checksum = std.hash::crc32(source);
        if (checksum != 0xcbf4_3926) {
            return 24;
        }
        std.hash::md5_digest md5 = std.hash::md5(source);
        if ((md5.bytes[0] != 0x25) || (md5.bytes[15] != 0x0b)) {
            return 25;
        }
        std.hash::sha1_digest sha1 = std.hash::sha1(source);
        if ((sha1.bytes[0] != 0xf7) || (sha1.bytes[19] != 0x41)) {
            return 26;
        }
        std.hash::sha256_digest sha256 = std.hash::sha256(source);
        if ((sha256.bytes[0] != 0x15) || (sha256.bytes[31] != 0x25)) {
            return 27;
        }
        std.hash::sha512_digest sha512 = std.hash::sha512(source);
        if ((sha512.bytes[0] != 0xd9) || (sha512.bytes[63] != 0x85)) {
            return 28;
        }

        constexpr str bit_source = "AAAAAAAAA";
        std.bits::lsb_reader reader = {};
        u64 first = std.bits::read(bit_source, &reader, 1);
        if (first != 1) {
            return 29;
        }
        u64 wide = std.bits::read(bit_source, &reader, 64);
        if (wide != 0xa0a0_a0a0_a0a0_a0a0) {
            return 31;
        }
        std.bits::align_byte(&reader);
        if ((reader.byte_index != 9) || (reader.bit_count != 0)) {
            return 33;
        }

        std.bytes::append_u8(&value, 0xaa);
        std.bytes::append_u16_le(&value, 0x1122);
        std.bytes::append_u32_le(&value, 0x3344_5566);
        std.bytes::append_u64_le(&value, 0x7788_99aa_bbcc_ddee);
        bytes reserved = std.bytes::with_capacity(16);
        test_observe(&reserved);
        return 0;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 34;
    } catch (core::utf8_error error) {
        error as void;
        return 23;
    } catch (std.bits::read_error error) {
        error as void;
        return 32;
    } catch (std.bytes::bytes_error error) {
        error as void;
        return 59;
    }
}

async i32 main() {
    try {
        i32 sync_status = sync_checks();
        if (sync_status != 0) {
            return sync_status;
        }
        try {
            task<i32> operation = async_checks();
            i32 async_status = await move operation;
            return async_status;
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 40};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
