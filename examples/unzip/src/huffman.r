module example.unzip.huffman;

import example.unzip.bitstream::{read_zip_bits};
import example.unzip.error::{ZipError, ZipErrorCode};

protected const usize MAX_BITS = 15;
protected const usize MAX_SYMBOLS = 288;

struct Huffman {
    protected u16[16] count;
    protected u16[MAX_SYMBOLS] symbols;
    protected usize symbol_count;
};

struct Tables {
    Huffman literals;
    Huffman distances;
};

protected Huffman build(const u8[] lengths, bool allow_single) throws ZipError {
    usize symbol_count = len(lengths);
    throw (symbol_count > MAX_SYMBOLS) ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = symbol_count,
        .message = "Huffman alphabet exceeds implementation bound",
    };

    Huffman table = {};
    table.symbol_count = symbol_count;
    usize used = 0;
    usize symbol = 0;
    while (symbol < symbol_count) {
        u8 length = lengths[symbol];
        throw (length > 15) ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = symbol,
            .message = "Huffman code is longer than 15 bits",
        };
        if (length != 0) {
            usize slot = length as usize;
            table.count[slot] += 1;
            used += 1;
        }
        symbol += 1;
    }

    throw (used == 0) ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = 0,
        .message = "empty Huffman alphabet",
    };

    i32 left = 1;
    usize bits = 1;
    while (bits <= MAX_BITS) {
        left *= 2;
        left -= table.count[bits] as i32;
        throw (left < 0) ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = bits,
            .message = "oversubscribed Huffman tree",
        };
        bits += 1;
    }

    bool one_bit_single = (used == 1) && (table.count[1] == 1);
    if (left > 0) {
        bool accepted_single = (allow_single == true) && (one_bit_single == true);
        throw (accepted_single == false) ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = used,
            .message = "incomplete Huffman tree",
        };
    }

    u16[17] offsets = {};
    usize bits_2 = 1;
    while (bits_2 <= MAX_BITS) {
        usize next = bits_2 + 1;
        offsets[next] = (offsets[bits_2] + table.count[bits_2]) as u16;
        bits_2 += 1;
    }

    usize symbol_2 = 0;
    while (symbol_2 < symbol_count) {
        u8 length = lengths[symbol_2];
        if (length != 0) {
            usize length_index = length as usize;
            usize destination = offsets[length_index] as usize;
            table.symbols[destination] = symbol_2 as u16;
            offsets[length_index] += 1;
        }
        symbol_2 += 1;
    }

    return table;
}

u16 decode_symbol(const u8[] input,
    std.bits::lsb_reader* cursor,
    const Huffman* table) throws ZipError {
    u32 code = 0;
    u32 first = 0;
    usize index = 0;
    usize bits = 1;

    while (bits <= MAX_BITS) {
        u32 next_bit = read_zip_bits(input, cursor, 1);
        code |= next_bit;
        u32 count = table->count[bits] as u32;
        u32 limit = first + count;
        if (code < limit) {
            u32 delta = code - first;
            usize symbol_index = index + (delta as usize);
            throw (symbol_index >= table->symbol_count) ZipError {
                .code = ZipErrorCode::InvalidDeflate,
                .offset = cursor->byte_index,
                .message = "Huffman symbol index is outside alphabet",
            };
            u16 value = table->symbols[symbol_index];
            return value;
        }

        index += count as usize;
        first = limit << 1;
        code <<= 1;
        bits += 1;
    }

    throw ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = cursor->byte_index,
        .message = "invalid Huffman prefix",
    };
}

Tables fixed_tables() throws ZipError {
    u8[288] literal_lengths = {};
    usize index = 0;
    while (index <= 143) {
        literal_lengths[index] = 8;
        index += 1;
    }
    while (index <= 255) {
        literal_lengths[index] = 9;
        index += 1;
    }
    while (index <= 279) {
        literal_lengths[index] = 7;
        index += 1;
    }
    while (index <= 287) {
        literal_lengths[index] = 8;
        index += 1;
    }

    u8[32] distance_lengths = {};
    usize index_2 = 0;
    while (index_2 < 32) {
        distance_lengths[index_2] = 5;
        index_2 += 1;
    }

    const u8[] literal_view = &literal_lengths;
    const u8[] distance_view = &distance_lengths;
    Huffman literals = build(literal_view, false);
    Huffman distances = build(distance_view, false);
    Tables tables = {
        .literals = literals,
        .distances = distances,
    };
    return tables;
}

Tables dynamic_tables(const u8[] input,
    std.bits::lsb_reader* cursor) throws ZipError {
    u32 hlit_bits = read_zip_bits(input, cursor, 5);
    u32 hdist_bits = read_zip_bits(input, cursor, 5);
    u32 hclen_bits = read_zip_bits(input, cursor, 4);
    usize literal_count = (hlit_bits as usize) + 257;
    usize distance_count = (hdist_bits as usize) + 1;
    usize code_count = (hclen_bits as usize) + 4;

    throw ((literal_count > 286) || (distance_count > 32)) ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = cursor->byte_index,
        .message = "dynamic Huffman alphabet is out of range",
    };

    u8[19] order = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5,
        11, 4, 12, 3, 13, 2, 14, 1, 15,
    };
    u8[19] code_lengths = {};
    usize index = 0;
    while (index < code_count) {
        u32 value = read_zip_bits(input, cursor, 3);
        usize slot = order[index] as usize;
        code_lengths[slot] = value as u8;
        index += 1;
    }

    const u8[] code_view = &code_lengths;
    Huffman code_table = build(code_view, false);
    const Huffman* codes = &code_table;
    u8[320] lengths = {};
    usize total = literal_count + distance_count;
    usize index_3 = 0;
    u8 previous = 0;

    while (index_3 < total) {
        u16 symbol = decode_symbol(input, cursor, codes);
        if (symbol <= 15) {
            previous = symbol as u8;
            lengths[index_3] = previous;
            index_3 += 1;
            continue;
        }

        usize repeat = 0;
        u8 value = 0;
        if (symbol == 16) {
            throw (index_3 == 0) ZipError {
                .code = ZipErrorCode::InvalidDeflate,
                .offset = cursor->byte_index,
                .message = "repeat code has no previous length",
            };
            u32 extra = read_zip_bits(input, cursor, 2);
            repeat = (extra as usize) + 3;
            value = previous;
        } else {
            if (symbol == 17) {
                u32 extra = read_zip_bits(input, cursor, 3);
                repeat = (extra as usize) + 3;
                value = 0;
            } else {
                if (symbol == 18) {
                    u32 extra = read_zip_bits(input, cursor, 7);
                    repeat = (extra as usize) + 11;
                    value = 0;
                } else {
                    throw ZipError {
                        .code = ZipErrorCode::InvalidDeflate,
                        .offset = cursor->byte_index,
                        .message = "invalid code-length repeat symbol",
                    };
                }
            }
        }

        usize remaining = total - index_3;
        throw (repeat > remaining) ZipError {
            .code = ZipErrorCode::InvalidDeflate,
            .offset = cursor->byte_index,
            .message = "code-length repeat exceeds alphabet",
        };
        usize repeated = 0;
        while (repeated < repeat) {
            lengths[index_3] = value;
            index_3 += 1;
            repeated += 1;
        }
        previous = value;
    }

    throw (lengths[256] == 0) ZipError {
        .code = ZipErrorCode::InvalidDeflate,
        .offset = cursor->byte_index,
        .message = "literal tree has no end-of-block symbol",
    };

    const u8[] literal_view = lengths[0..literal_count];
    Huffman literals = build(literal_view, true);
    usize distance_end = literal_count + distance_count;
    const u8[] distance_view = lengths[literal_count..distance_end];
    bool has_distance_code = false;
    usize distance_index = literal_count;
    while (distance_index < distance_end) {
        if (lengths[distance_index] != 0) {
            has_distance_code = true;
        }
        distance_index += 1;
    }
    Huffman distances = {};
    if (has_distance_code == true) {
        distances = build(distance_view, true);
    } else {
        distances.symbol_count = distance_count;
    }
    Tables tables = {
        .literals = literals,
        .distances = distances,
    };
    return tables;
}
