module example.unzip.deflate;

import example.unzip.bitstream::{read_zip_bits};
import example.unzip.error::{ZipError, ZipErrorCode};
import example.unzip.huffman::{Huffman, Tables, decode_symbol, fixed_tables, dynamic_tables};

protected void decode_compressed(const u8[] input,
    std.bits::lsb_reader* cursor,
    const Huffman* literals,
    const Huffman* distances,
    u8[] output,
    usize* written,
    usize expected_size) throws ZipError {
    u16[29] length_base = {
        3, 4, 5, 6, 7, 8, 9, 10,
        11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115,
        131, 163, 195, 227, 258,
    };
    u8[29] length_extra = {
        0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4,
        5, 5, 5, 5, 0,
    };
    u16[30] distance_base = {
        1, 2, 3, 4, 5, 7, 9, 13,
        17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073,
        4097, 6145, 8193, 12289, 16385, 24577,
    };
    u8[30] distance_extra = {
        0, 0, 0, 0, 1, 1, 2, 2,
        3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10,
        11, 11, 12, 12, 13, 13,
    };

    usize capacity = len(output);
    while (true) {
        u16 symbol = decode_symbol(input, cursor, literals);
        if (symbol < 256) {
            throw ((*written >= expected_size) || (*written >= capacity)) ZipError {
                .code = ZipErrorCode::LimitExceeded,
                .offset = *written,
                .message = "deflate literal exceeds declared output size",
            };
            output[*written] = symbol as u8;
            *written += 1;
            continue;
        }

        if (symbol == 256) {
            return;
        }

        throw (symbol > 285) ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = cursor->byte_index,
            .message = "reserved literal/length symbol",
        };

        usize length_index = (symbol - 257) as usize;
        u8 length_extra_bits = length_extra[length_index];
        u32 length_delta = read_zip_bits(input, cursor, length_extra_bits);
        usize match_length = (length_base[length_index] as usize) +
                             (length_delta as usize);

        u16 distance_symbol = decode_symbol(input, cursor, distances);
        throw (distance_symbol >= 30) ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = cursor->byte_index,
            .message = "reserved distance symbol",
        };

        usize distance_index = distance_symbol as usize;
        u8 distance_extra_bits = distance_extra[distance_index];
        u32 distance_delta = read_zip_bits(input, cursor, distance_extra_bits);
        usize distance = (distance_base[distance_index] as usize) +
                         (distance_delta as usize);
        throw ((distance == 0) || (distance > *written)) ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = cursor->byte_index,
            .message = "distance points before produced output",
        };

        throw ((*written > expected_size) || (*written > capacity)) ZipError {
            .code = ZipErrorCode::Internal,
            .offset = *written,
            .message = "decoder output cursor escaped bounds",
        };
        usize expected_remaining = expected_size - *written;
        usize capacity_remaining = capacity - *written;
        throw ((match_length > expected_remaining) || (match_length > capacity_remaining)) ZipError {
            .code = ZipErrorCode::LimitExceeded,
            .offset = *written,
            .message = "back-reference exceeds declared output size",
        };

        usize source = *written - distance;
        usize copied = 0;
        while (copied < match_length) {
            output[*written] = output[source];
            *written += 1;
            source += 1;
            copied += 1;
        }
    }
}

protected void decode_stored(const u8[] input,
    std.bits::lsb_reader* cursor,
    u8[] output,
    usize* written,
    usize expected_size) throws ZipError {
    cursor->align_byte();
    u32 length = read_zip_bits(input, cursor, 16);
    u32 inverse = read_zip_bits(input, cursor, 16);
    u32 expected_inverse = length ^ 0xffff;
    throw (inverse != expected_inverse) ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = cursor->byte_index,
        .message = "stored block LEN/NLEN mismatch",
    };

    usize block_size = length as usize;
    usize capacity = len(output);
    throw ((*written > expected_size) || (*written > capacity)) ZipError {
        .code = ZipErrorCode::Internal,
        .offset = *written,
        .message = "stored block cursor escaped bounds",
    };
    usize expected_remaining = expected_size - *written;
    usize capacity_remaining = capacity - *written;
    throw ((block_size > expected_remaining) || (block_size > capacity_remaining)) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = *written,
        .message = "stored block exceeds declared output size",
    };

    usize index = 0;
    while (index < block_size) {
        u32 byte = read_zip_bits(input, cursor, 8);
        output[*written] = byte as u8;
        *written += 1;
        index += 1;
    }

}

usize decode(const u8[] input,
    u8[] output,
    usize expected_size) throws ZipError {
    usize capacity = len(output);
    throw (expected_size > capacity) ZipError {
        .code = ZipErrorCode::LimitExceeded,
        .offset = expected_size,
        .message = "entry exceeds worker output buffer",
    };

    std.bits::lsb_reader cursor = {};
    usize written = 0;
    bool final_block = false;

    while (final_block == false) {
        u32 final_bit = read_zip_bits(input, &cursor, 1);
        final_block = final_bit == 1;
        u32 block_type = read_zip_bits(input, &cursor, 2);

        switch (block_type) {
            case 0:
                decode_stored(
                    input,
                    &cursor,
                    output,
                    &written,
                    expected_size);
                break;
            case 1:
                Tables tables = fixed_tables();
                decode_compressed(
                    input,
                    &cursor,
                    &tables.literals,
                    &tables.distances,
                    output,
                    &written,
                    expected_size);
                break;
            case 2:
                Tables tables = dynamic_tables(input, &cursor);
                decode_compressed(
                    input,
                    &cursor,
                    &tables.literals,
                    &tables.distances,
                    output,
                    &written,
                    expected_size);
                break;
            default:
                throw ZipError {
                    .code = ZipErrorCode::InvalidDeflate,
                    .offset = cursor.byte_index,
                    .message = "reserved deflate block type",
                };
        }
    }

    throw (written != expected_size) {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = written,
        .message = "decoded size differs from central directory",
    };

    usize input_size = len(input);
    throw (cursor.byte_index != input_size) ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = cursor.byte_index,
        .message = "trailing bytes after final deflate block",
    };

    return written;
}
