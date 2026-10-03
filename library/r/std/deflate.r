module std.deflate;

/* R-SLIB-DEFLATE-0001: DEFLATE (RFC 1951) with the zlib (RFC 1950) and gzip (RFC 1952)
   wrappers. Both directions are resumable state machines over caller-owned buffers: a step
   consumes a prefix of its input, produces a prefix of its output and reports what it needs
   next. Allocation happens only in create; steps neither allocate nor block. */

enum format {
    rfc1951,   // raw DEFLATE
    rfc1950,   // zlib wrapper: 2-byte header, Adler-32 trailer
    rfc1952,   // gzip wrapper: 10-byte header with optional fields, CRC-32 and size trailer
};

enum flush {
    none,      // accumulate; emit blocks only when the block buffer is full
    sync,      // emit every pending symbol and align to a byte boundary
    finish,    // emit everything, mark the final block and write the trailer
};

enum state {
    need_input,    // the input was consumed; more input is required to continue
    need_output,   // the output is full; call again with the unconsumed input suffix
    end,           // the stream is complete; unconsumed input belongs to the caller
};

enum error_code {
    corrupt_stream,      // malformed header, block, code, distance or trailer
    checksum_mismatch,   // zlib or gzip trailer disagrees with the produced bytes
    invalid_level,       // compression level above 9
    invalid_window,      // window not a power of two in [256, 32768]
    unsupported,         // preset dictionary or reserved gzip flag
    output_limit,        // inflating would exceed the declared output limit
    finished,            // a step after end
    poisoned,            // a step after a failure
};

error error {
    error_code code;
    usize offset;   // total input bytes consumed by the stream when the failure was detected
};

struct progress {
    usize consumed;
    usize produced;
    state state;
};

protected const usize MAX_BITS = 15usize;
protected const usize MAX_SYMBOLS = 288usize;
protected const usize WINDOW_MAX = 32768usize;
protected const usize WINDOW_MIN = 256usize;
protected const usize ONE_SHOT_CHUNK = 4096usize;
protected const u32 ADLER_MODULUS = 65521u32;
/* The most bytes whose Adler-32 sums stay below 2^32 when the modulo is taken only after them. */
protected const usize ADLER_BLOCK = 5552usize;
/* RFC 1952 CRC-32 of each byte value (reflected polynomial 0xedb88320). */
protected const u32[256] CRC_TABLE = {
    0x00000000u32, 0x77073096u32, 0xee0e612cu32, 0x990951bau32, 0x076dc419u32, 0x706af48fu32,
    0xe963a535u32, 0x9e6495a3u32, 0x0edb8832u32, 0x79dcb8a4u32, 0xe0d5e91eu32, 0x97d2d988u32,
    0x09b64c2bu32, 0x7eb17cbdu32, 0xe7b82d07u32, 0x90bf1d91u32, 0x1db71064u32, 0x6ab020f2u32,
    0xf3b97148u32, 0x84be41deu32, 0x1adad47du32, 0x6ddde4ebu32, 0xf4d4b551u32, 0x83d385c7u32,
    0x136c9856u32, 0x646ba8c0u32, 0xfd62f97au32, 0x8a65c9ecu32, 0x14015c4fu32, 0x63066cd9u32,
    0xfa0f3d63u32, 0x8d080df5u32, 0x3b6e20c8u32, 0x4c69105eu32, 0xd56041e4u32, 0xa2677172u32,
    0x3c03e4d1u32, 0x4b04d447u32, 0xd20d85fdu32, 0xa50ab56bu32, 0x35b5a8fau32, 0x42b2986cu32,
    0xdbbbc9d6u32, 0xacbcf940u32, 0x32d86ce3u32, 0x45df5c75u32, 0xdcd60dcfu32, 0xabd13d59u32,
    0x26d930acu32, 0x51de003au32, 0xc8d75180u32, 0xbfd06116u32, 0x21b4f4b5u32, 0x56b3c423u32,
    0xcfba9599u32, 0xb8bda50fu32, 0x2802b89eu32, 0x5f058808u32, 0xc60cd9b2u32, 0xb10be924u32,
    0x2f6f7c87u32, 0x58684c11u32, 0xc1611dabu32, 0xb6662d3du32, 0x76dc4190u32, 0x01db7106u32,
    0x98d220bcu32, 0xefd5102au32, 0x71b18589u32, 0x06b6b51fu32, 0x9fbfe4a5u32, 0xe8b8d433u32,
    0x7807c9a2u32, 0x0f00f934u32, 0x9609a88eu32, 0xe10e9818u32, 0x7f6a0dbbu32, 0x086d3d2du32,
    0x91646c97u32, 0xe6635c01u32, 0x6b6b51f4u32, 0x1c6c6162u32, 0x856530d8u32, 0xf262004eu32,
    0x6c0695edu32, 0x1b01a57bu32, 0x8208f4c1u32, 0xf50fc457u32, 0x65b0d9c6u32, 0x12b7e950u32,
    0x8bbeb8eau32, 0xfcb9887cu32, 0x62dd1ddfu32, 0x15da2d49u32, 0x8cd37cf3u32, 0xfbd44c65u32,
    0x4db26158u32, 0x3ab551ceu32, 0xa3bc0074u32, 0xd4bb30e2u32, 0x4adfa541u32, 0x3dd895d7u32,
    0xa4d1c46du32, 0xd3d6f4fbu32, 0x4369e96au32, 0x346ed9fcu32, 0xad678846u32, 0xda60b8d0u32,
    0x44042d73u32, 0x33031de5u32, 0xaa0a4c5fu32, 0xdd0d7cc9u32, 0x5005713cu32, 0x270241aau32,
    0xbe0b1010u32, 0xc90c2086u32, 0x5768b525u32, 0x206f85b3u32, 0xb966d409u32, 0xce61e49fu32,
    0x5edef90eu32, 0x29d9c998u32, 0xb0d09822u32, 0xc7d7a8b4u32, 0x59b33d17u32, 0x2eb40d81u32,
    0xb7bd5c3bu32, 0xc0ba6cadu32, 0xedb88320u32, 0x9abfb3b6u32, 0x03b6e20cu32, 0x74b1d29au32,
    0xead54739u32, 0x9dd277afu32, 0x04db2615u32, 0x73dc1683u32, 0xe3630b12u32, 0x94643b84u32,
    0x0d6d6a3eu32, 0x7a6a5aa8u32, 0xe40ecf0bu32, 0x9309ff9du32, 0x0a00ae27u32, 0x7d079eb1u32,
    0xf00f9344u32, 0x8708a3d2u32, 0x1e01f268u32, 0x6906c2feu32, 0xf762575du32, 0x806567cbu32,
    0x196c3671u32, 0x6e6b06e7u32, 0xfed41b76u32, 0x89d32be0u32, 0x10da7a5au32, 0x67dd4accu32,
    0xf9b9df6fu32, 0x8ebeeff9u32, 0x17b7be43u32, 0x60b08ed5u32, 0xd6d6a3e8u32, 0xa1d1937eu32,
    0x38d8c2c4u32, 0x4fdff252u32, 0xd1bb67f1u32, 0xa6bc5767u32, 0x3fb506ddu32, 0x48b2364bu32,
    0xd80d2bdau32, 0xaf0a1b4cu32, 0x36034af6u32, 0x41047a60u32, 0xdf60efc3u32, 0xa867df55u32,
    0x316e8eefu32, 0x4669be79u32, 0xcb61b38cu32, 0xbc66831au32, 0x256fd2a0u32, 0x5268e236u32,
    0xcc0c7795u32, 0xbb0b4703u32, 0x220216b9u32, 0x5505262fu32, 0xc5ba3bbeu32, 0xb2bd0b28u32,
    0x2bb45a92u32, 0x5cb36a04u32, 0xc2d7ffa7u32, 0xb5d0cf31u32, 0x2cd99e8bu32, 0x5bdeae1du32,
    0x9b64c2b0u32, 0xec63f226u32, 0x756aa39cu32, 0x026d930au32, 0x9c0906a9u32, 0xeb0e363fu32,
    0x72076785u32, 0x05005713u32, 0x95bf4a82u32, 0xe2b87a14u32, 0x7bb12baeu32, 0x0cb61b38u32,
    0x92d28e9bu32, 0xe5d5be0du32, 0x7cdcefb7u32, 0x0bdbdf21u32, 0x86d3d2d4u32, 0xf1d4e242u32,
    0x68ddb3f8u32, 0x1fda836eu32, 0x81be16cdu32, 0xf6b9265bu32, 0x6fb077e1u32, 0x18b74777u32,
    0x88085ae6u32, 0xff0f6a70u32, 0x66063bcau32, 0x11010b5cu32, 0x8f659effu32, 0xf862ae69u32,
    0x616bffd3u32, 0x166ccf45u32, 0xa00ae278u32, 0xd70dd2eeu32, 0x4e048354u32, 0x3903b3c2u32,
    0xa7672661u32, 0xd06016f7u32, 0x4969474du32, 0x3e6e77dbu32, 0xaed16a4au32, 0xd9d65adcu32,
    0x40df0b66u32, 0x37d83bf0u32, 0xa9bcae53u32, 0xdebb9ec5u32, 0x47b2cf7fu32, 0x30b5ffe9u32,
    0xbdbdf21cu32, 0xcabac28au32, 0x53b39330u32, 0x24b4a3a6u32, 0xbad03605u32, 0xcdd70693u32,
    0x54de5729u32, 0x23d967bfu32, 0xb3667a2eu32, 0xc4614ab8u32, 0x5d681b02u32, 0x2a6f2b94u32,
    0xb40bbe37u32, 0xc30c8ea1u32, 0x5a05df1bu32, 0x2d02ef8du32,
};

/* Slicing by eight: slice k holds the CRC-32 of a byte followed by k zero bytes, so eight
   lookups advance the CRC over eight bytes. Slice 0 is CRC_TABLE. */
protected u32[2048] crc_slices() {
    u32[2048] table = {};
    for (usize value = 0usize; value < 256usize; value += 1usize) { table[value] = CRC_TABLE[value]; }
    for (usize slice = 1usize; slice < 8usize; slice += 1usize) {
        for (usize value = 0usize; value < 256usize; value += 1usize) {
            u32 previous = table[(slice - 1usize) * 256usize + value];
            table[slice * 256usize + value] = (previous >> 8usize) ^ CRC_TABLE[(previous & 255u32) as usize];
        }
    }
    return table;
}

protected const u32[2048] CRC_SLICES = crc_slices();

/* RFC 1951 3.2.5: base values and extra bits of the length codes 257..285 and of the distance
   codes, and the order of the code-length code lengths in a dynamic block header. */
protected const u16[29] LENGTH_BASE = {
    3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
    35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
};
protected const u8[29] LENGTH_EXTRA = {
    0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
    3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
};
protected const u16[30] DISTANCE_BASE = {
    1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
    257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
};
protected const u8[30] DISTANCE_EXTRA = {
    0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
    7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
};
protected const u8[19] CODE_LENGTH_ORDER = {
    16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
};
protected const usize MIN_MATCH = 3usize;
protected const usize MAX_MATCH = 258usize;

/* The length code (0..28) of each match length 3..258, at length - 3. */
protected u8[256] length_code_table() {
    u8[256] table = {};
    usize code = 0usize;
    for (usize length = MIN_MATCH; length <= MAX_MATCH; length += 1usize) {
        while ((code < 28usize) && (length >= (LENGTH_BASE[code + 1usize] as usize))) { code += 1usize; }
        table[length - MIN_MATCH] = code as u8;
    }
    return table;
}

/* The distance code of each distance minus one: below 256 at that value, from 256 on at
   256 + (value >> 7), as in zlib. */
protected u8[512] distance_code_table() {
    u8[512] table = {};
    for (usize code = 0usize; code < 30usize; code += 1usize) {
        usize first = (DISTANCE_BASE[code] as usize) - 1usize;
        usize stop = first + (1usize << (DISTANCE_EXTRA[code] as usize));
        for (usize value = first; value < stop; value += 1usize) {
            if (value < 256usize) {
                table[value] = code as u8;
            } else {
                table[256usize + (value >> 7usize)] = code as u8;
            }
        }
    }
    return table;
}

protected const u8[256] LENGTH_CODE = length_code_table();
protected const u8[512] DISTANCE_CODE = distance_code_table();

protected usize length_slot(usize length) {
    return LENGTH_CODE[(length - MIN_MATCH) & 255usize] as usize;
}

protected usize distance_slot(usize distance) {
    usize value = distance - 1usize;
    if (value < 256usize) { return DISTANCE_CODE[value] as usize; }
    return DISTANCE_CODE[256usize + ((value >> 7usize) & 255usize)] as usize;
}

/* Canonical Huffman decoding table: codes per length and symbols ordered by code. A code of up
   to FAST_BITS bits also decodes with one lookup in `fast`, at every index whose low bits are
   the code read first bit first: entry = symbol << 4 | length; 0 sends a longer or invalid
   code to the canonical search. */
protected const usize FAST_BITS = 9usize;
protected const usize FAST_SIZE = 512usize;

protected struct huffman {
    u16[16] count;
    u16[288] symbols;
    u16[512] fast;
    usize symbol_count;
};

/* One decoded symbol; status 0 = decoded, 1 = more bits are required, 2 = invalid code. */
protected struct decoded {
    u16 symbol;
    u32 used;
    u8 status;
};

/* Output position shared by the emitting stages of the inflater. The window holds the bytes
   of the calls before this one: window_fill of them, the next written at window_pos. */
protected struct cursor {
    usize out_pos;
    usize window_pos;
    usize window_fill;
    usize total_out;
    u32 check_a;
    u32 check_b;
    usize check_pos;
};

protected bool window_is_valid(usize window_size) {
    if ((window_size < WINDOW_MIN) || (window_size > WINDOW_MAX)) { return false; }
    usize probe = WINDOW_MIN;
    while (probe <= WINDOW_MAX) {
        if (probe == window_size) { return true; }
        probe *= 2usize;
    }
    return false;
}

protected usize window_log2(usize window_size) {
    usize bits = 0usize;
    usize probe = 1usize;
    while (probe < window_size) {
        probe *= 2usize;
        bits += 1usize;
    }
    return bits;
}

/* The running stream checksum, Adler-32 for zlib and CRC-32 for gzip. */
protected struct checksum_pair {
    u32 a;
    u32 b;
};

/* Advances the checksum over bytes. Adler-32 takes its modulo once per ADLER_BLOCK bytes and
   sums sixteen bytes per step. */
protected checksum_pair checksum_update(format form, u32 a, u32 b, const u8[] bytes) {
    usize count = len(bytes);
    u32 first = a;
    u32 second = b;
    if (form == format::rfc1950) {
        usize index = 0usize;
        while (index < count) {
            usize stop = (count - index > ADLER_BLOCK) ? index + ADLER_BLOCK : count;
            const u8[] block = bytes[index..stop];
            /* Over the n bytes b0..b(n-1) of a block the first sum grows by their sum and the
               second by n * first + sum((n - j) * bj), the sequential sums in closed form:
               one reduction with no chain from byte to byte. Within ADLER_BLOCK bytes every
               term stays below 2^32 (zlib's NMAX bound), so u32 arithmetic is exact. */
            u32 sum = 0u32;
            u32 weighted = 0u32;
            u32 weight = (len(block) & 0xffffusize) as u32;
            for (usize offset = 0usize; offset < len(block); offset += 1usize) {
                u32 value = block[offset] as u32;
                sum += value;
                weighted += weight * value;
                weight -= 1u32;
            }
            second += ((len(block) & 0xffffusize) as u32) * first + weighted;
            first += sum;
            first %= ADLER_MODULUS;
            second %= ADLER_MODULUS;
            index = stop;
        }
    }
    if (form == format::rfc1952) {
        usize index = 0usize;
        while (count - index >= 8usize) {
            const u8[] chunk = bytes[index..index + 8usize];
            u32 low = first ^ ((chunk[0] as u32) | ((chunk[1] as u32) << 8usize) |
                               ((chunk[2] as u32) << 16usize) | ((chunk[3] as u32) << 24usize));
            first = CRC_SLICES[1792usize + ((low & 255u32) as usize)] ^
                    CRC_SLICES[1536usize + (((low >> 8usize) & 255u32) as usize)] ^
                    CRC_SLICES[1280usize + (((low >> 16usize) & 255u32) as usize)] ^
                    CRC_SLICES[1024usize + ((low >> 24usize) as usize)] ^
                    CRC_SLICES[768usize + (chunk[4] as usize)] ^ CRC_SLICES[512usize + (chunk[5] as usize)] ^
                    CRC_SLICES[256usize + (chunk[6] as usize)] ^ CRC_TABLE[chunk[7] as usize];
            index += 8usize;
        }
        while (index < count) {
            usize slot = ((first ^ (bytes[index] as u32)) & 0xffu32) as usize;
            first = CRC_TABLE[slot] ^ (first >> 8usize);
            index += 1usize;
        }
    }
    return checksum_pair { .a = first, .b = second };
}

/* Copies source to the start of target; the shorter of the two bounds the copy. */
protected void copy_bytes(u8[] target, const u8[] source) {
    for (usize index = 0usize; (index < len(target)) && (index < len(source)); index += 1usize) {
        target[index] = source[index];
    }
}

/* Sixteen bytes of a buffer as a value, so that a copy within the buffer reads its source
   before it borrows the destination. */
protected u8[16] read_chunk(const u8[] buffer, usize from) {
    u8[16] chunk = {};
    const u8[] source = buffer[from..from + 16usize];
    for (usize index = 0usize; index < 16usize; index += 1usize) { chunk[index] = source[index]; }
    return chunk;
}

protected void write_chunk(u8[] buffer, usize to, u8[16] chunk) {
    u8[] target = buffer[to..to + 16usize];
    for (usize index = 0usize; index < 16usize; index += 1usize) { target[index] = chunk[index]; }
}

protected u8[64] read_block(const u8[] buffer, usize from) {
    u8[64] block = {};
    const u8[] source = buffer[from..from + 64usize];
    for (usize index = 0usize; index < 64usize; index += 1usize) { block[index] = source[index]; }
    return block;
}

protected void write_block(u8[] buffer, usize to, u8[64] block) {
    u8[] target = buffer[to..to + 64usize];
    for (usize index = 0usize; index < 64usize; index += 1usize) { target[index] = block[index]; }
}

/* Each byte with its bits reversed. */
protected u8[256] reversed_bytes() {
    u8[256] table = {};
    for (usize value = 1usize; value < 256usize; value += 1usize) {
        table[value] = (((table[value >> 1usize] as usize) >> 1usize) | ((value & 1usize) << 7usize)) as u8;
    }
    return table;
}

protected const u8[256] REVERSED_BYTE = reversed_bytes();

/* A canonical code of width 1..15 read first bit first: the 15-bit reversal from two byte
   reversals, shifted down to the width. */
protected usize reversed_code(u32 code, usize width) {
    usize value = (code as usize) & 0x7fffusize;
    usize reversed = ((REVERSED_BYTE[value & 255usize] as usize) << 7usize) |
                     ((REVERSED_BYTE[(value >> 8usize) & 255usize] as usize) >> 1usize);
    return reversed >> ((15usize - width) & 15usize);
}

/* Builds the canonical table from code lengths; false when the lengths do not form a
   complete prefix code (a single one-bit code is complete enough when allow_single). */
protected bool build(huffman* table, const u8[] lengths, bool allow_single) {
    usize symbol_count = len(lengths);
    if (symbol_count > MAX_SYMBOLS) { return false; }
    for (usize slot = 0usize; slot < 16usize; slot += 1usize) { table->count[slot] = 0; }
    table->symbol_count = symbol_count;
    usize used = 0usize;
    for (usize symbol = 0usize; symbol < symbol_count; symbol += 1usize) {
        u8 length = lengths[symbol];
        if (length > 15) { return false; }
        if (length != 0) {
            usize slot = length as usize;
            table->count[slot] += 1;
            used += 1usize;
        }
    }
    if (used == 0usize) { return false; }
    i32 left = 1;
    for (usize bits = 1usize; bits <= MAX_BITS; bits += 1usize) {
        left *= 2;
        left -= table->count[bits] as i32;
        if (left < 0) { return false; }
    }
    if (left > 0) {
        bool single = (used == 1usize) && (table->count[1] == 1);
        if ((allow_single == false) || (single == false)) { return false; }
    }
    u16[17] offsets = {};
    for (usize bits = 1usize; bits <= MAX_BITS; bits += 1usize) {
        usize next = bits + 1usize;
        offsets[next] = (offsets[bits] + table->count[bits]) as u16;
    }
    for (usize symbol = 0usize; symbol < symbol_count; symbol += 1usize) {
        u8 length = lengths[symbol];
        if (length != 0) {
            usize slot = (length as usize) & 15usize;
            usize destination = offsets[slot] as usize;
            table->symbols[destination] = symbol as u16;
            offsets[slot] += 1;
        }
    }
    /* RFC 1951 3.2.2: the canonical code of each symbol, entered at every index of the fast
       table whose low bits are that code read first bit first. */
    for (usize slot = 0usize; slot < FAST_SIZE; slot += 1usize) { table->fast[slot] = 0; }
    u32[16] next_code = {};
    u32 code = 0u32;
    for (usize bits = 1usize; bits <= MAX_BITS; bits += 1usize) {
        code = (code + (table->count[bits - 1usize] as u32)) << 1usize;
        next_code[bits] = code;
    }
    for (usize symbol = 0usize; symbol < symbol_count; symbol += 1usize) {
        usize length = (lengths[symbol] as usize) & 15usize;
        if (length != 0usize) {
            u32 assigned = next_code[length];
            next_code[length] = assigned + 1u32;
            if (length <= FAST_BITS) {
                usize step = 1usize << length;
                u16 entry = ((symbol << 4usize) | length) as u16;
                usize first_slot = reversed_code(assigned, length);
                for (usize slot = first_slot; slot < FAST_SIZE; slot += step) {
                    table->fast[slot] = entry;
                }
            }
        }
    }
    return true;
}

protected bool build_fixed(huffman* literals, huffman* distances) {
    u8[288] literal_lengths = {};
    for (usize index = 0usize; index < 144usize; index += 1usize) { literal_lengths[index] = 8; }
    for (usize index = 144usize; index < 256usize; index += 1usize) { literal_lengths[index] = 9; }
    for (usize index = 256usize; index < 280usize; index += 1usize) { literal_lengths[index] = 7; }
    for (usize index = 280usize; index < 288usize; index += 1usize) { literal_lengths[index] = 8; }
    u8[32] distance_lengths = {};
    for (usize index = 0usize; index < 32usize; index += 1usize) { distance_lengths[index] = 5; }
    const u8[] literal_view = &literal_lengths;
    const u8[] distance_view = &distance_lengths;
    bool literals_ok = build(literals, literal_view, false);
    if (literals_ok == false) { return false; }
    return build(distances, distance_view, false);
}

/* Decodes one code from the low bits of the accumulator without consuming them: one lookup
   for a short code, otherwise the canonical search one bit at a time. */
protected decoded decode_symbol(const huffman* table, u64 bits, u32 available) {
    u16 entry = table->fast[(bits & 511u64) as usize];
    u32 width = (entry & 15u16) as u32;
    if ((width != 0u32) && (width <= available)) {
        return decoded { .symbol = (entry >> 4usize) as u16, .used = width, .status = 0 };
    }
    u32 code = 0u32;
    u32 first = 0u32;
    usize index = 0usize;
    for (usize length = 1usize; length <= MAX_BITS; length += 1usize) {
        if (length > (available as usize)) {
            return decoded { .symbol = 0, .used = 0u32, .status = 1 };
        }
        code |= ((bits >> (length - 1usize)) & 1u64) as u32;
        u32 count = table->count[length] as u32;
        if (code < first + count) {
            usize slot = index + ((code - first) as usize);
            if (slot >= table->symbol_count) {
                return decoded { .symbol = 0, .used = 0u32, .status = 2 };
            }
            return decoded { .symbol = table->symbols[slot], .used = length as u32, .status = 0 };
        }
        index += count as usize;
        first = (first + count) << 1usize;
        code <<= 1usize;
    }
    return decoded { .symbol = 0, .used = 0u32, .status = 2 };
}

protected u32 extract(u64 bits, u32 offset, u32 width) {
    if (width == 0u32) { return 0u32; }
    u64 shifted = bits >> (offset as usize);
    u64 mask = (1u64 << (width as usize)) - 1u64;
    return (shifted & mask) as u32;
}

/* The checksum covers the emitted bytes later, a range at a time (flush_checksum), and the
   window takes them once per call (update_window). */
protected void emit_byte(cursor* at, u8 value, u8[] output) {
    output[at->out_pos] = value;
    at->out_pos += 1usize;
    at->total_out += 1usize;
}

/* The byte `distance` positions before the next output byte: in this call's output, or in
   the window of the calls before. */
protected u8 history_byte(const cursor* at, const u8[] output, const u8[] window, usize distance) {
    if (distance <= at->out_pos) { return output[at->out_pos - distance]; }
    usize back = distance - at->out_pos;
    usize mask = len(window) - 1usize;
    return window[(at->window_pos + len(window) - back) & mask];
}

/* Brings the window up to the bytes this call produced: the last window bytes of the history
   stay, in the ring order the window keeps. */
protected void update_window(cursor* at, u8[] window, const u8[] produced) {
    usize size = len(window);
    usize count = len(produced);
    if (count >= size) {
        copy_bytes(window, produced[count - size..count]);
        at->window_pos = 0usize;
        at->window_fill = size;
        return;
    }
    usize mask = size - 1usize;
    if (mask >= len(window)) { return; }
    usize position = at->window_pos;
    for (usize index = 0usize; index < len(produced); index += 1usize) {
        window[(position + index) & mask] = produced[index];
    }
    at->window_pos = (position + count) & mask;
    usize fill = at->window_fill + count;
    if (fill > size) { fill = size; }
    at->window_fill = fill;
}

/* Brings the cursor's checksum up to every byte emitted into output so far. */
protected void flush_checksum(cursor* at, const u8[] output, format form) {
    if (at->check_pos == at->out_pos) { return; }
    checksum_pair next = checksum_update(form, at->check_a, at->check_b,
                                         output[at->check_pos..at->out_pos]);
    at->check_a = next.a;
    at->check_b = next.b;
    at->check_pos = at->out_pos;
}

/* The gzip header stage that follows the part just completed, in header order. */
protected u8 gzip_next_stage(u8 flags, u8 completed) {
    if ((completed < 1) && ((flags & 4) != 0)) { return 1; }
    if ((completed < 3) && ((flags & 8) != 0)) { return 3; }
    if ((completed < 4) && ((flags & 16) != 0)) { return 4; }
    if ((completed < 5) && ((flags & 2) != 0)) { return 5; }
    return 6;
}

/* R-SLIB-DEFLATE-0003: the inflater. Stages: 0 header, 1 gzip extra length, 2 gzip extra,
   3 gzip name, 4 gzip comment, 5 gzip header CRC, 6 block header, 7 stored length,
   8 stored copy, 9 dynamic table header, 10 code-length codes, 11 code lengths,
   12 symbol, 13 match copy, 14 trailer, 15 end. */
struct inflater {
    protected array<u8> window;
    protected huffman literals;
    protected huffman distances;
    protected huffman code_table;
    protected u8[320] lengths;
    protected u8[19] code_lengths;
    protected u8[10] header;
    protected format form;
    protected usize window_size;
    protected usize window_mask;
    protected usize output_limit;
    protected usize window_pos;
    protected usize window_fill;
    protected usize total_in;
    protected usize total_out;
    protected u64 bits;
    protected u32 bit_count;
    protected u8 stage;
    protected bool final_block;
    protected usize literal_count;
    protected usize distance_count;
    protected usize code_count;
    protected usize length_index;
    protected u8 previous_length;
    protected usize remaining;
    protected usize copy_distance;
    protected u32 check_a;
    protected u32 check_b;
    protected u8 gzip_flags;
    protected usize trailer_index;
    protected bool poisoned;
};

inflater inflater::create(format form, usize window_size, usize output_limit)
    throws error, std.alloc::alloc_error {
    bool valid = window_is_valid(window_size);
    throw (valid == false) error { .code = error_code::invalid_window, .offset = 0usize };
    array<u8> window = std.alloc::bytes(window_size, 0u8);
    inflater result = inflater {
        .window = move window,
        .form = form,
        .window_size = window_size,
        .window_mask = window_size - 1usize,
        .output_limit = output_limit,
    };
    result.reset();
    return move result;
}

/* Returns the state to the start of a new stream; the window allocation is kept. */
void inflater::reset(inflater* this) {
    this->window_pos = 0usize;
    this->window_fill = 0usize;
    this->total_in = 0usize;
    this->total_out = 0usize;
    this->bits = 0u64;
    this->bit_count = 0u32;
    this->stage = 0;
    this->final_block = false;
    this->literal_count = 0usize;
    this->distance_count = 0usize;
    this->code_count = 0usize;
    this->length_index = 0usize;
    this->previous_length = 0;
    this->remaining = 0usize;
    this->copy_distance = 0usize;
    this->check_a = 0u32;
    this->check_b = 0u32;
    if (this->form == format::rfc1950) { this->check_a = 1u32; }
    if (this->form == format::rfc1952) { this->check_a = 0xffffffffu32; }
    this->gzip_flags = 0;
    this->trailer_index = 0usize;
    this->poisoned = false;
}

/* Totals over the whole stream so far, including the current step. */
usize inflater::consumed_bytes(const inflater* this) { return this->total_in; }
usize inflater::produced_bytes(const inflater* this) { return this->total_out; }

/* Step outcomes shared by the inflater helpers: 0 continue, 1 more input is required,
   2 corrupt stream, 3 output is full, 4 output limit reached, 5 the stage completed. */
protected const u8 STEP_CONTINUE = 0;
protected const u8 STEP_NEED_INPUT = 1;
protected const u8 STEP_CORRUPT = 2;
protected const u8 STEP_NEED_OUTPUT = 3;
protected const u8 STEP_LIMIT = 4;
protected const u8 STEP_DONE = 5;

protected void take_bits(u64* bits, u32* bit_count, u32 width) {
    *bits >>= (width as usize);
    *bit_count -= width;
}

/* Stage 11: one code length or one repeat; STEP_DONE once every length is known and the
   literal and distance tables are built. */
protected u8 read_code_lengths(inflater* this, u64* bits, u32* bit_count) {
    usize total = this->literal_count + this->distance_count;
    if (this->length_index >= total) {
        if (this->lengths[256] == 0) { return STEP_CORRUPT; }
        u8[320] lengths_copy = this->lengths;
        const u8[] literal_view = lengths_copy[0usize..this->literal_count];
        bool literals_ok = build(&this->literals, literal_view, true);
        if (literals_ok == false) { return STEP_CORRUPT; }
        bool has_distance = false;
        for (usize index = this->literal_count; index < total; index += 1usize) {
            if (lengths_copy[index] != 0) { has_distance = true; }
        }
        if (has_distance == true) {
            const u8[] distance_view = lengths_copy[this->literal_count..total];
            bool distances_ok = build(&this->distances, distance_view, true);
            if (distances_ok == false) { return STEP_CORRUPT; }
        } else {
            for (usize slot = 0usize; slot < 16usize; slot += 1usize) { this->distances.count[slot] = 0; }
            for (usize slot = 0usize; slot < FAST_SIZE; slot += 1usize) { this->distances.fast[slot] = 0; }
            this->distances.symbol_count = this->distance_count;
        }
        return STEP_DONE;
    }
    decoded code = decode_symbol(&this->code_table, *bits, *bit_count);
    if (code.status == 1) { return STEP_NEED_INPUT; }
    if (code.status == 2) { return STEP_CORRUPT; }
    usize repeat = 1usize;
    u8 value = 0;
    u32 extra_width = 0u32;
    if (code.symbol <= 15) {
        value = code.symbol as u8;
    } else {
        if (code.symbol == 16) {
            if (this->length_index == 0usize) { return STEP_CORRUPT; }
            extra_width = 2u32;
        } else {
            extra_width = (code.symbol == 17) ? 3u32 : 7u32;
        }
        if (*bit_count < code.used + extra_width) { return STEP_NEED_INPUT; }
        u32 extra = extract(*bits, code.used, extra_width);
        if (code.symbol == 16) {
            repeat = (extra as usize) + 3usize;
            value = this->previous_length;
        } else {
            if (code.symbol == 17) { repeat = (extra as usize) + 3usize; }
            else { repeat = (extra as usize) + 11usize; }
        }
    }
    take_bits(bits, bit_count, code.used + extra_width);
    if (repeat > total - this->length_index) { return STEP_CORRUPT; }
    for (usize count = 0usize; count < repeat; count += 1usize) {
        this->lengths[this->length_index] = value;
        this->length_index += 1usize;
    }
    this->previous_length = value;
    return STEP_CONTINUE;
}

/* Stage 12: one literal (emitted), the end of block (STEP_DONE) or one match, which sets
   the copy state for stage 13 and reports STEP_CONTINUE with this->remaining nonzero. */
protected u8 decode_next(inflater* this, u64* bits, u32* bit_count, cursor* at, u8[] output) {
    decoded code = decode_symbol(&this->literals, *bits, *bit_count);
    if (code.status == 1) { return STEP_NEED_INPUT; }
    if (code.status == 2) { return STEP_CORRUPT; }
    /* Only a literal needs room now: the end of the block needs none, and a match waits for
       room in stage 13, so a stream that fills its output exactly still ends. */
    if (code.symbol < 256) {
        if (at->out_pos >= len(output)) { return STEP_NEED_OUTPUT; }
        if (at->total_out >= this->output_limit) { return STEP_LIMIT; }
        take_bits(bits, bit_count, code.used);
        emit_byte(at, code.symbol as u8, output);
        return STEP_CONTINUE;
    }
    if (code.symbol == 256) {
        take_bits(bits, bit_count, code.used);
        return STEP_DONE;
    }
    usize length_slot_index = (code.symbol as usize) - 257usize;
    if (length_slot_index >= 29usize) { return STEP_CORRUPT; }
    u32 length_width = LENGTH_EXTRA[length_slot_index] as u32;
    u32 used = code.used + length_width;
    if (*bit_count < used) { return STEP_NEED_INPUT; }
    decoded distance_code = decode_symbol(&this->distances, *bits >> (used as usize), *bit_count - used);
    if (distance_code.status == 1) { return STEP_NEED_INPUT; }
    if (distance_code.status == 2) { return STEP_CORRUPT; }
    usize distance_slot_index = distance_code.symbol as usize;
    if (distance_slot_index >= 30usize) { return STEP_CORRUPT; }
    u32 distance_width = DISTANCE_EXTRA[distance_slot_index] as u32;
    u32 total_used = used + distance_code.used + distance_width;
    if (*bit_count < total_used) { return STEP_NEED_INPUT; }
    u32 length_delta = extract(*bits, code.used, length_width);
    u32 distance_delta = extract(*bits, used + distance_code.used, distance_width);
    usize match_length = (LENGTH_BASE[length_slot_index] as usize) + (length_delta as usize);
    usize distance = (DISTANCE_BASE[distance_slot_index] as usize) + (distance_delta as usize);
    if ((distance > at->window_fill + at->out_pos) || (distance > this->window_size)) { return STEP_CORRUPT; }
    take_bits(bits, bit_count, total_used);
    this->remaining = match_length;
    this->copy_distance = distance;
    return STEP_CONTINUE;
}

/* Stage 13: copies the match from the history until it is complete or the output is full. */
protected u8 copy_match(inflater* this, cursor* at, u8[] output, const u8[] window) {
    usize output_size = len(output);
    while (this->remaining != 0usize) {
        if (at->out_pos >= output_size) { return STEP_NEED_OUTPUT; }
        if (at->total_out >= this->output_limit) { return STEP_LIMIT; }
        u8 byte = history_byte(at, output, window, this->copy_distance);
        emit_byte(at, byte, output);
        this->remaining -= 1usize;
    }
    return STEP_DONE;
}

/* What the fast decoder leaves behind: the accumulator, the input position and its outcome. */
protected struct fast_step {
    u64 bits;
    u32 bit_count;
    usize pos;
    u8 outcome;
};

/* Stage 12 while the accumulator can hold a whole literal or match and the output has room
   for the longest match (zlib's inflate_fast): literals and matches without the stage
   machine. STEP_CONTINUE leaves the next symbol to decode_next, STEP_DONE reports the end of
   the block, STEP_CORRUPT an invalid code or distance. */
protected fast_step decode_fast(const inflater* this, const u8[] input, usize start, u64 bits_in,
                                u32 count_in, cursor* at, u8[] output, const u8[] window) {
    u64 bits = bits_in;
    u32 bit_count = count_in;
    usize pos = start;
    usize first_out = at->out_pos;
    usize out_pos = first_out;
    usize out_end = len(output);
    /* The output limit ends the output as the end of the slice does. */
    usize allowed = 0usize;
    if (this->output_limit > at->total_out) { allowed = this->output_limit - at->total_out; }
    if (allowed < out_end - out_pos) { out_end = out_pos + allowed; }
    u8 outcome = STEP_CONTINUE;
    usize window_mask = len(window) - 1usize;
    usize history = at->window_fill;
    usize window_base = at->window_pos + len(window);
    if ((out_end > len(output)) || (window_mask >= len(window))) {
        return fast_step { .bits = bits, .bit_count = bit_count, .pos = pos, .outcome = outcome };
    }
    while (out_pos < out_end) {
        if (out_end - out_pos < MAX_MATCH) { break; }
        while ((bit_count <= 56u32) && (pos < len(input))) {
            bits |= (input[pos] as u64) << (bit_count as usize);
            bit_count += 8u32;
            pos += 1usize;
        }
        /* The longest literal or match: a 15-bit code, 5 extra bits, a 15-bit distance code
           and 13 extra bits. */
        if (bit_count < 48u32) { break; }
        u16 entry = this->literals.fast[(bits & 511u64) as usize];
        u32 used = (entry & 15u16) as u32;
        usize symbol = (entry >> 4usize) as usize;
        if (used == 0u32) {
            decoded long_code = decode_symbol(&this->literals, bits, bit_count);
            if (long_code.status != 0) {
                outcome = STEP_CORRUPT;
                break;
            }
            used = long_code.used;
            symbol = long_code.symbol as usize;
        }
        if (symbol < 256usize) {
            output[out_pos] = (symbol & 255usize) as u8;
            out_pos += 1usize;
            bits >>= (used as usize);
            bit_count -= used;
            continue;
        }
        if (symbol == 256usize) {
            bits >>= (used as usize);
            bit_count -= used;
            outcome = STEP_DONE;
            break;
        }
        usize slot = symbol - 257usize;
        if (slot >= 29usize) {
            outcome = STEP_CORRUPT;
            break;
        }
        u32 length_width = LENGTH_EXTRA[slot] as u32;
        u32 length_bits = used + length_width;
        u64 rest = bits >> (length_bits as usize);
        u16 distance_entry = this->distances.fast[(rest & 511u64) as usize];
        u32 distance_used = (distance_entry & 15u16) as u32;
        usize distance_symbol = (distance_entry >> 4usize) as usize;
        if (distance_used == 0u32) {
            decoded long_distance = decode_symbol(&this->distances, rest, bit_count - length_bits);
            if (long_distance.status != 0) {
                outcome = STEP_CORRUPT;
                break;
            }
            distance_used = long_distance.used;
            distance_symbol = long_distance.symbol as usize;
        }
        if (distance_symbol >= 30usize) {
            outcome = STEP_CORRUPT;
            break;
        }
        u32 distance_width = DISTANCE_EXTRA[distance_symbol] as u32;
        usize match_length = (LENGTH_BASE[slot] as usize) + (extract(bits, used, length_width) as usize);
        usize distance = (DISTANCE_BASE[distance_symbol] as usize) +
                         (extract(bits, length_bits + distance_used, distance_width) as usize);
        if ((distance > history + (out_pos - first_out) + first_out) || (distance > this->window_size)) {
            outcome = STEP_CORRUPT;
            break;
        }
        usize copy_end = out_pos + match_length;
        if (copy_end > out_end) { break; }
        u32 total_used = length_bits + distance_used + distance_width;
        bits >>= (total_used as usize);
        bit_count -= total_used;
        if (distance > out_pos) {
            /* The start of the match lies in the window of the calls before. */
            usize back = distance - out_pos;
            usize source = window_base - back;
            while ((back > 0usize) && (out_pos < copy_end)) {
                output[out_pos] = window[source & window_mask];
                source += 1usize;
                back -= 1usize;
                out_pos += 1usize;
            }
        }
        usize from = out_pos - distance;
        /* At a distance of at least a block (64) or a chunk (16) bytes, the source of each
           block or chunk is complete before the bytes it is copied to begin. */
        if (distance >= 64usize) {
            while (copy_end - out_pos >= 64usize) {
                u8[64] block = read_block(output, from);
                write_block(output, out_pos, block);
                from += 64usize;
                out_pos += 64usize;
            }
        }
        if (distance >= 16usize) {
            while (copy_end - out_pos >= 16usize) {
                u8[16] chunk = read_chunk(output, from);
                write_chunk(output, out_pos, chunk);
                from += 16usize;
                out_pos += 16usize;
            }
        }
        while ((out_pos < copy_end) && (from < out_pos)) {
            output[out_pos] = output[from];
            from += 1usize;
            out_pos += 1usize;
        }
    }
    at->total_out += out_pos - first_out;
    at->out_pos = out_pos;
    return fast_step { .bits = bits, .bit_count = bit_count, .pos = pos, .outcome = outcome };
}

/* Stage 14: the trailer bytes, then their verification against the running checksum. */
protected u8 read_trailer(inflater* this, u64* bits, u32* bit_count, const cursor* at) {
    usize needed = (this->form == format::rfc1950) ? 4usize : 8usize;
    if (this->trailer_index < needed) {
        if (*bit_count < 8u32) { return STEP_NEED_INPUT; }
        this->header[this->trailer_index] = extract(*bits, 0u32, 8u32) as u8;
        take_bits(bits, bit_count, 8u32);
        this->trailer_index += 1usize;
        return STEP_CONTINUE;
    }
    u8[10] header = this->header;
    if (this->form == format::rfc1950) {
        u32 expected = (at->check_b << 16usize) | at->check_a;
        u32 found = ((header[0] as u32) << 24usize) | ((header[1] as u32) << 16usize) |
                    ((header[2] as u32) << 8usize) | (header[3] as u32);
        if (found == expected) { return STEP_DONE; }
        return STEP_CORRUPT;
    }
    u32 crc = ((header[3] as u32) << 24usize) | ((header[2] as u32) << 16usize) |
              ((header[1] as u32) << 8usize) | (header[0] as u32);
    u32 size = ((header[7] as u32) << 24usize) | ((header[6] as u32) << 16usize) |
               ((header[5] as u32) << 8usize) | (header[4] as u32);
    u32 produced = (at->total_out & 0xffffffffusize) as u32;
    bool matches = (crc == (at->check_a ^ 0xffffffffu32)) && (size == produced);
    if (matches == true) { return STEP_DONE; }
    return STEP_CORRUPT;
}

/* Stage 0 for gzip: one header byte; STEP_DONE after the tenth. */
protected u8 read_gzip_header_byte(inflater* this, u64* bits, u32* bit_count) {
    if (*bit_count < 8u32) { return STEP_NEED_INPUT; }
    u8 byte = extract(*bits, 0u32, 8u32) as u8;
    take_bits(bits, bit_count, 8u32);
    usize index = this->length_index;
    this->header[index] = byte;
    this->length_index = index + 1usize;
    if ((index == 0usize) && (byte != 0x1f)) { return STEP_CORRUPT; }
    if ((index == 1usize) && (byte != 0x8b)) { return STEP_CORRUPT; }
    if ((index == 2usize) && (byte != 8)) { return STEP_CORRUPT; }
    if (index == 3usize) {
        if ((byte & 0xe0) != 0) { return STEP_LIMIT; }
        this->gzip_flags = byte;
    }
    if (index == 9usize) {
        this->length_index = 0usize;
        return STEP_DONE;
    }
    return STEP_CONTINUE;
}

/* Stage 0 for zlib: the two header bytes; STEP_LIMIT reports a preset dictionary. */
protected u8 read_zlib_header(inflater* this, u64* bits, u32* bit_count) {
    if (*bit_count < 16u32) { return STEP_NEED_INPUT; }
    u32 header = extract(*bits, 0u32, 16u32);
    take_bits(bits, bit_count, 16u32);
    u32 cmf = header & 255u32;
    if ((cmf & 15u32) != 8u32) { return STEP_CORRUPT; }
    u32 cinfo = cmf >> 4usize;
    if (cinfo > 7u32) { return STEP_CORRUPT; }
    usize declared = 1usize << ((cinfo + 8u32) as usize);
    if (declared > this->window_size) { return STEP_CORRUPT; }
    u32 flg = header >> 8usize;
    u32 check = (cmf * 256u32 + flg) % 31u32;
    if (check != 0u32) { return STEP_CORRUPT; }
    if ((flg & 32u32) != 0u32) { return STEP_LIMIT; }
    return STEP_DONE;
}

progress inflater::inflate(inflater* this, const u8[] input, u8[] output) throws error {
    throw (this->poisoned == true) error { .code = error_code::poisoned, .offset = this->total_in };
    throw (this->stage == 15) error { .code = error_code::finished, .offset = this->total_in };
    array<u8> window_owner = core::replace(&this->window, std.array::create::<u8>());
    u8[] window = std.array::as_slice_mut(&window_owner);
    cursor at = cursor {
        .out_pos = 0usize,
        .window_pos = this->window_pos,
        .window_fill = this->window_fill,
        .total_out = this->total_out,
        .check_a = this->check_a,
        .check_b = this->check_b,
        .check_pos = 0usize,
    };
    u64 bits = this->bits;
    u32 bit_count = this->bit_count;
    u8 stage = this->stage;
    usize pos = 0usize;
    usize input_size = len(input);
    usize output_size = len(output);
    state result = state::need_input;
    bool stop = false;
    bool failed = false;
    error_code failure = error_code::corrupt_stream;
    format form = this->form;
    while (stop == false) {
        while ((bit_count <= 56u32) && (pos < input_size)) {
            bits |= (input[pos] as u64) << (bit_count as usize);
            bit_count += 8u32;
            pos += 1usize;
        }
        u8 outcome = STEP_CONTINUE;
        switch (stage) {
        case 0:
            if (form == format::rfc1951) {
                stage = 6;
                break;
            }
            outcome = (form == format::rfc1950) ? read_zlib_header(this, &bits, &bit_count)
                                                : read_gzip_header_byte(this, &bits, &bit_count);
            if (outcome == STEP_LIMIT) {
                failure = error_code::unsupported;
                outcome = STEP_CORRUPT;
            }
            if (outcome == STEP_DONE) {
                stage = (form == format::rfc1950) ? 6 : gzip_next_stage(this->gzip_flags, 0);
                outcome = STEP_CONTINUE;
            }
            break;
        case 1:
            if (bit_count < 16u32) { outcome = STEP_NEED_INPUT; break; }
            this->remaining = extract(bits, 0u32, 16u32) as usize;
            take_bits(&bits, &bit_count, 16u32);
            stage = 2;
            break;
        case 2:
            if (this->remaining == 0usize) {
                stage = gzip_next_stage(this->gzip_flags, 2);
                break;
            }
            if (bit_count < 8u32) { outcome = STEP_NEED_INPUT; break; }
            take_bits(&bits, &bit_count, 8u32);
            this->remaining -= 1usize;
            break;
        case 3:
            fallthrough;
        case 4:
            if (bit_count < 8u32) { outcome = STEP_NEED_INPUT; break; }
            u32 name_byte = extract(bits, 0u32, 8u32);
            take_bits(&bits, &bit_count, 8u32);
            if (name_byte == 0u32) { stage = gzip_next_stage(this->gzip_flags, stage); }
            break;
        case 5:
            if (bit_count < 16u32) { outcome = STEP_NEED_INPUT; break; }
            take_bits(&bits, &bit_count, 16u32);
            stage = 6;
            break;
        case 6:
            if (bit_count < 3u32) { outcome = STEP_NEED_INPUT; break; }
            u32 header = extract(bits, 0u32, 3u32);
            take_bits(&bits, &bit_count, 3u32);
            this->final_block = (header & 1u32) == 1u32;
            u32 kind = header >> 1usize;
            if (kind == 0u32) { stage = 7; }
            if (kind == 2u32) { stage = 9; }
            if (kind == 3u32) { outcome = STEP_CORRUPT; }
            if (kind == 1u32) {
                bool built = build_fixed(&this->literals, &this->distances);
                if (built == false) { outcome = STEP_CORRUPT; break; }
                stage = 12;
            }
            break;
        case 7:
            u32 drop_bits = bit_count & 7u32;
            take_bits(&bits, &bit_count, drop_bits);
            if (bit_count < 32u32) { outcome = STEP_NEED_INPUT; break; }
            u32 length = extract(bits, 0u32, 16u32);
            u32 inverse = extract(bits, 16u32, 16u32);
            take_bits(&bits, &bit_count, 32u32);
            if ((length ^ 0xffffu32) != inverse) { outcome = STEP_CORRUPT; break; }
            this->remaining = length as usize;
            stage = 8;
            break;
        case 8:
            if (this->remaining == 0usize) {
                stage = (this->final_block == true) ? 14 : 6;
                break;
            }
            if (at.out_pos >= output_size) { outcome = STEP_NEED_OUTPUT; break; }
            if (at.total_out >= this->output_limit) { outcome = STEP_LIMIT; break; }
            /* A stored block is byte aligned: the whole bytes of the accumulator come first,
               then the rest of the block straight from the input. */
            if (bit_count >= 8u32) {
                u8 stored_byte = extract(bits, 0u32, 8u32) as u8;
                take_bits(&bits, &bit_count, 8u32);
                emit_byte(&at, stored_byte, output);
                this->remaining -= 1usize;
                break;
            }
            if (pos >= input_size) { outcome = STEP_NEED_INPUT; break; }
            usize stored_count = this->remaining;
            if (input_size - pos < stored_count) { stored_count = input_size - pos; }
            if (output_size - at.out_pos < stored_count) { stored_count = output_size - at.out_pos; }
            if (this->output_limit - at.total_out < stored_count) {
                stored_count = this->output_limit - at.total_out;
            }
            copy_bytes(output[at.out_pos..at.out_pos + stored_count], input[pos..pos + stored_count]);
            at.out_pos += stored_count;
            at.total_out += stored_count;
            pos += stored_count;
            this->remaining -= stored_count;
            break;
        case 9:
            if (bit_count < 14u32) { outcome = STEP_NEED_INPUT; break; }
            u32 hlit = extract(bits, 0u32, 5u32);
            u32 hdist = extract(bits, 5u32, 5u32);
            u32 hclen = extract(bits, 10u32, 4u32);
            take_bits(&bits, &bit_count, 14u32);
            this->literal_count = (hlit as usize) + 257usize;
            this->distance_count = (hdist as usize) + 1usize;
            this->code_count = (hclen as usize) + 4usize;
            if ((this->literal_count > 286usize) || (this->distance_count > 30usize)) {
                outcome = STEP_CORRUPT;
                break;
            }
            for (usize index = 0usize; index < 19usize; index += 1usize) { this->code_lengths[index] = 0; }
            this->length_index = 0usize;
            stage = 10;
            break;
        case 10:
            if (this->length_index >= this->code_count) {
                u8[19] code_copy = this->code_lengths;
                const u8[] code_view = &code_copy;
                bool built = build(&this->code_table, code_view, false);
                if (built == false) { outcome = STEP_CORRUPT; break; }
                this->length_index = 0usize;
                this->previous_length = 0;
                for (usize index = 0usize; index < 320usize; index += 1usize) { this->lengths[index] = 0; }
                stage = 11;
                break;
            }
            if (bit_count < 3u32) { outcome = STEP_NEED_INPUT; break; }
            usize slot = CODE_LENGTH_ORDER[this->length_index] as usize;
            this->code_lengths[slot] = extract(bits, 0u32, 3u32) as u8;
            take_bits(&bits, &bit_count, 3u32);
            this->length_index += 1usize;
            break;
        case 11:
            outcome = read_code_lengths(this, &bits, &bit_count);
            if (outcome == STEP_DONE) {
                stage = 12;
                outcome = STEP_CONTINUE;
            }
            break;
        case 12:
            fast_step fast = decode_fast(this, input, pos, bits, bit_count, &at, output, window);
            bits = fast.bits;
            bit_count = fast.bit_count;
            pos = fast.pos;
            outcome = fast.outcome;
            if (outcome == STEP_CONTINUE) {
                outcome = decode_next(this, &bits, &bit_count, &at, output);
            }
            if (outcome == STEP_DONE) {
                stage = (this->final_block == true) ? 14 : 6;
                outcome = STEP_CONTINUE;
            }
            if ((outcome == STEP_CONTINUE) && (this->remaining != 0usize)) { stage = 13; }
            break;
        case 13:
            outcome = copy_match(this, &at, output, window);
            if (outcome == STEP_DONE) {
                stage = 12;
                outcome = STEP_CONTINUE;
            }
            break;
        case 14:
            u32 padding = bit_count & 7u32;
            take_bits(&bits, &bit_count, padding);
            if (form == format::rfc1951) {
                stage = 15;
                break;
            }
            flush_checksum(&at, output, form);
            outcome = read_trailer(this, &bits, &bit_count, &at);
            if (outcome == STEP_CORRUPT) { failure = error_code::checksum_mismatch; }
            if (outcome == STEP_DONE) {
                stage = 15;
                outcome = STEP_CONTINUE;
            }
            break;
        default:
            /* Whole bytes pulled into the accumulator past the trailer belong to the caller. */
            pos -= (bit_count / 8u32) as usize;
            bits = 0u64;
            bit_count = 0u32;
            result = state::end;
            stop = true;
            break;
        }
        if (outcome == STEP_NEED_INPUT) { stop = true; }
        if (outcome == STEP_NEED_OUTPUT) {
            result = state::need_output;
            stop = true;
        }
        if (outcome == STEP_LIMIT) {
            failure = error_code::output_limit;
            failed = true;
            stop = true;
        }
        if (outcome == STEP_CORRUPT) {
            failed = true;
            stop = true;
        }
    }
    flush_checksum(&at, output, form);
    /* A finished stream reads no history again. */
    if (stage != 15) { update_window(&at, window, output[0usize..at.out_pos]); }
    this->window = move window_owner;
    this->window_pos = at.window_pos;
    this->window_fill = at.window_fill;
    this->total_out = at.total_out;
    this->check_a = at.check_a;
    this->check_b = at.check_b;
    this->bits = bits;
    this->bit_count = bit_count;
    this->stage = stage;
    this->total_in += pos;
    if (failed == true) {
        this->poisoned = true;
        throw error { .code = failure, .offset = this->total_in };
    }
    return progress { .consumed = pos, .produced = at.out_pos, .state = result };
}

/* Grows the output of a one-shot call to at least `minimum` bytes, keeping its first `used`. */
protected void grow_output(array<u8>* buffer, usize used, usize minimum)
    throws std.alloc::alloc_error {
    usize size = len(*buffer) * 2usize;
    if (size < minimum) { size = minimum; }
    array<u8> bigger = std.alloc::bytes(size, 0u8);
    {
        u8[] target = std.array::as_slice_mut(&bigger);
        const u8[] source = std.array::as_slice(buffer);
        copy_bytes(target, source[0usize..used]);
    }
    array<u8> previous = core::replace(buffer, move bigger);
    drop previous;
}

/* The first `used` bytes of a one-shot output, in an array of exactly that length: a short
   tail is popped, a long one is left behind by a copy. */
protected array<u8> exact_output(array<u8> buffer, usize used) throws std.alloc::alloc_error {
    if (len(buffer) - used <= used / 16usize) {
        while (len(buffer) > used) { std.array::pop(&buffer) as void; }
        return move buffer;
    }
    array<u8> result = std.alloc::bytes(used, 0u8);
    {
        u8[] target = std.array::as_slice_mut(&result);
        const u8[] source = std.array::as_slice(&buffer);
        copy_bytes(target, source[0usize..used]);
    }
    drop buffer;
    return move result;
}

/* The first size of a one-shot inflate output: four times the input, at least one window
   and at most the output limit, which a stream that reaches the limit fills exactly. */
protected usize first_output_size(usize input_size, usize limit) {
    usize size = WINDOW_MAX * 2usize;
    if (input_size < (1usize << 40usize)) { size = input_size * 4usize + ONE_SHOT_CHUNK; }
    if (size < WINDOW_MAX * 2usize) { size = WINDOW_MAX * 2usize; }
    if (size > limit) { size = limit; }
    if (size < ONE_SHOT_CHUNK) { size = ONE_SHOT_CHUNK; }
    return size;
}

/* R-SLIB-DEFLATE-0005: one-shot decompression of one complete stream, written straight into
   the result. */
array<u8> inflate(const u8[] input, format form, usize output_limit)
    throws error, std.alloc::alloc_error {
    inflater engine = inflater::create(form, WINDOW_MAX, output_limit);
    usize input_size = len(input);
    array<u8> result = std.alloc::bytes(first_output_size(input_size, output_limit), 0u8);
    usize produced = 0usize;
    usize consumed = 0usize;
    while (true) {
        u8[] room = std.array::as_slice_mut(&result);
        u8[] out_view = room[produced..len(room)];
        const u8[] rest = input[consumed..input_size];
        progress step = engine.inflate(rest, out_view);
        consumed += step.consumed;
        produced += step.produced;
        if (step.state == state::end) {
            throw (consumed != input_size) error { .code = error_code::corrupt_stream, .offset = consumed };
            return exact_output(move result, produced);
        }
        throw (step.state == state::need_input) error { .code = error_code::corrupt_stream, .offset = consumed };
        grow_output(&result, produced, produced + ONE_SHOT_CHUNK);
    }
}

/* R-SLIB-DEFLATE-0004: the deflater. LZ77 over a sliding buffer of twice the window with
   hash chains, greedy matching below level 4 and lazy matching from level 4, blocks chosen
   by their exact bit cost among stored, fixed and dynamic Huffman coding. */
protected const usize MIN_LOOKAHEAD = 262usize;
protected const usize PENDING_EXTRA = 1024usize;
protected const usize STORED_OVERHEAD_BITS = 40usize;
/* The empty chain link. Positions lie below twice the window (at most 65536) and only those
   with three bytes before fill enter the chains, so 0xffff is never a position. */
protected const u16 NIL = 0xffffu16;

protected struct level_config {
    usize good;
    usize lazy;
    usize nice;
    usize chain;
    bool lazy_matching;
};

protected level_config config_for(u8 level) {
    switch (level) {
    case 1: return level_config { .good = 4usize, .lazy = 4usize, .nice = 8usize, .chain = 4usize, .lazy_matching = false };
    case 2: return level_config { .good = 4usize, .lazy = 5usize, .nice = 16usize, .chain = 8usize, .lazy_matching = false };
    case 3: return level_config { .good = 4usize, .lazy = 6usize, .nice = 32usize, .chain = 32usize, .lazy_matching = false };
    case 4: return level_config { .good = 4usize, .lazy = 4usize, .nice = 16usize, .chain = 16usize, .lazy_matching = true };
    case 5: return level_config { .good = 8usize, .lazy = 16usize, .nice = 32usize, .chain = 32usize, .lazy_matching = true };
    case 6: return level_config { .good = 8usize, .lazy = 16usize, .nice = 128usize, .chain = 128usize, .lazy_matching = true };
    case 7: return level_config { .good = 8usize, .lazy = 32usize, .nice = 128usize, .chain = 256usize, .lazy_matching = true };
    case 8: return level_config { .good = 32usize, .lazy = 128usize, .nice = 258usize, .chain = 1024usize, .lazy_matching = true };
    case 9: return level_config { .good = 32usize, .lazy = 258usize, .nice = 258usize, .chain = 4096usize, .lazy_matching = true };
    default: return level_config { .good = 0usize, .lazy = 0usize, .nice = 0usize, .chain = 0usize, .lazy_matching = false };
    }
}

/* LSB-first bit writer over the staged output; length counts complete bytes. */
protected struct bit_writer {
    u64 acc;
    u32 count;
    usize length;
};

protected void put_bits(bit_writer* writer, u8[] pending, u32 value, u32 width) {
    writer->acc |= (value as u64) << (writer->count as usize);
    writer->count += width;
    while (writer->count >= 8u32) {
        pending[writer->length] = (writer->acc & 0xffu64) as u8;
        writer->length += 1usize;
        writer->acc >>= 8usize;
        writer->count -= 8u32;
    }
}

protected void align_bits(bit_writer* writer, u8[] pending) {
    if (writer->count != 0u32) {
        u32 padding = 8u32 - writer->count;
        put_bits(writer, pending, 0u32, padding);
    }
}

/* Length-limited canonical code lengths from frequencies (zlib's overflow adjustment); at
   least two codes are produced so that the block can always be written. The two lightest
   nodes, the lower index first on equal weights, merge until one tree remains; a binary
   heap keyed by weight and index yields them. */
protected void build_lengths(const u32[] freq, u8[] lengths, usize count, usize max_bits) {
    u32[576] weight = {};
    u16[576] parent = {};
    u16[576] heap = {};
    usize heap_size = 0usize;
    usize leaves = 0usize;
    u8[576] active = {};
    for (usize symbol = 0usize; symbol < count; symbol += 1usize) {
        lengths[symbol] = 0;
        if (freq[symbol] != 0u32) {
            weight[symbol] = freq[symbol];
            active[symbol] = 1;
            leaves += 1usize;
        }
    }
    usize forced = 0usize;
    while (leaves < 2usize) {
        if (active[forced] == 0) {
            active[forced] = 1;
            leaves += 1usize;
        }
        forced += 1usize;
    }
    for (usize node = 0usize; node < count; node += 1usize) {
        if (active[node] != 0) {
            /* Ascending index order keeps the heap property without sifting on equal weights;
               a lighter node rises. */
            usize position = heap_size;
            heap_size += 1usize;
            while (position > 0usize) {
                usize above = (position - 1usize) / 2usize;
                usize upper = heap[above] as usize;
                if (weight[upper] <= weight[node]) { break; }
                heap[position] = heap[above];
                position = above;
            }
            heap[position] = node as u16;
        }
    }
    usize node_count = count;
    while (heap_size > 1usize) {
        u16[2] taken = {};
        for (usize pick = 0usize; pick < 2usize; pick += 1usize) {
            taken[pick] = heap[0];
            heap_size -= 1usize;
            usize moving = heap[heap_size] as usize;
            usize position = 0usize;
            while (true) {
                usize child = position * 2usize + 1usize;
                if (child >= heap_size) { break; }
                usize right = child + 1usize;
                if (right < heap_size) {
                    usize left_node = heap[child] as usize;
                    usize right_node = heap[right] as usize;
                    bool right_first =
                        (weight[right_node] < weight[left_node]) ||
                        ((weight[right_node] == weight[left_node]) && (right_node < left_node));
                    if (right_first == true) { child = right; }
                }
                usize lower = heap[child] as usize;
                bool moving_first = (weight[moving] < weight[lower]) ||
                                    ((weight[moving] == weight[lower]) && (moving < lower));
                if (moving_first == true) { break; }
                heap[position] = heap[child];
                position = child;
            }
            heap[position] = moving as u16;
        }
        usize first = taken[0] as usize;
        usize second = taken[1] as usize;
        weight[node_count] = weight[first] + weight[second];
        parent[first] = node_count as u16;
        parent[second] = node_count as u16;
        /* The new node has the highest index, so it rises only past heavier nodes. */
        usize position = heap_size;
        heap_size += 1usize;
        while (position > 0usize) {
            usize above = (position - 1usize) / 2usize;
            usize upper = heap[above] as usize;
            if (weight[upper] <= weight[node_count]) { break; }
            heap[position] = heap[above];
            position = above;
        }
        heap[position] = node_count as u16;
        node_count += 1usize;
    }
    usize root = node_count - 1usize;
    u16[16] bl_count = {};
    usize overflow = 0usize;
    for (usize symbol = 0usize; symbol < count; symbol += 1usize) {
        bool is_leaf = (freq[symbol] != 0u32) || (parent[symbol] != 0);
        if (is_leaf == true) {
            usize depth = 0usize;
            usize node = symbol;
            while (node != root) {
                node = parent[node] as usize;
                depth += 1usize;
            }
            if (depth > max_bits) {
                depth = max_bits;
                overflow += 1usize;
            }
            bl_count[depth] += 1;
            lengths[symbol] = depth as u8;
        }
    }
    if (overflow == 0usize) { return; }
    /* Insertion by ascending frequency: the least frequent leaves take the longest codes. */
    u16[288] leaf_order = {};
    usize leaf_count = 0usize;
    for (usize symbol = 0usize; symbol < count; symbol += 1usize) {
        bool is_leaf = (freq[symbol] != 0u32) || (parent[symbol] != 0);
        if (is_leaf == true) {
            usize slot = leaf_count;
            while (slot > 0usize) {
                usize previous = leaf_order[slot - 1usize] as usize;
                if (freq[previous] <= freq[symbol]) { break; }
                leaf_order[slot] = leaf_order[slot - 1usize];
                slot -= 1usize;
            }
            leaf_order[slot] = symbol as u16;
            leaf_count += 1usize;
        }
    }
    while (overflow > 0usize) {
        usize bits = max_bits - 1usize;
        while (bl_count[bits] == 0) { bits -= 1usize; }
        bl_count[bits] -= 1;
        bl_count[bits + 1usize] += 2;
        bl_count[max_bits] -= 1;
        if (overflow >= 2usize) { overflow -= 2usize; } else { overflow = 0usize; }
    }
    usize next = 0usize;
    usize bits = max_bits;
    while (bits > 0usize) {
        usize pending = bl_count[bits] as usize;
        while (pending > 0usize) {
            usize symbol = leaf_order[next] as usize;
            lengths[symbol] = bits as u8;
            next += 1usize;
            pending -= 1usize;
        }
        bits -= 1usize;
    }
}

/* Canonical codes (RFC 1951 3.2.2), stored bit-reversed for the LSB-first writer. */
protected void assign_codes(const u8[] lengths, u16[] codes, usize count) {
    u16[16] bl_count = {};
    for (usize symbol = 0usize; symbol < count; symbol += 1usize) {
        usize length = lengths[symbol] as usize;
        if (length != 0usize) { bl_count[length] += 1; }
    }
    u32[16] next_code = {};
    u32 code = 0u32;
    for (usize bits = 1usize; bits <= MAX_BITS; bits += 1usize) {
        code = (code + (bl_count[bits - 1usize] as u32)) << 1usize;
        next_code[bits] = code;
    }
    for (usize symbol = 0usize; symbol < count; symbol += 1usize) {
        usize length = lengths[symbol] as usize;
        codes[symbol] = 0;
        if (length != 0usize) {
            codes[symbol] = (reversed_code(next_code[length], length) & 0xffffusize) as u16;
            next_code[length] += 1u32;
        }
    }
}

protected usize token_cost(const u32[] tokens, usize token_count, const u8[] literal_lengths,
                           const u8[] distance_lengths) {
    usize bits = literal_lengths[256] as usize;
    for (usize index = 0usize; index < token_count; index += 1usize) {
        u32 token = tokens[index];
        usize distance = (token >> 16usize) as usize;
        usize value = (token & 0xffffu32) as usize;
        if (distance == 0usize) {
            bits += literal_lengths[value] as usize;
        } else {
            usize slot = length_slot(value);
            bits += (literal_lengths[257usize + slot] as usize) + (LENGTH_EXTRA[slot] as usize);
            usize distance_code = distance_slot(distance);
            bits += (distance_lengths[distance_code] as usize) + (DISTANCE_EXTRA[distance_code] as usize);
        }
    }
    return bits;
}

protected void write_tokens(bit_writer* writer, u8[] pending, const u32[] tokens, usize token_count,
                            const u8[] literal_lengths, const u16[] literal_codes,
                            const u8[] distance_lengths, const u16[] distance_codes) {
    for (usize index = 0usize; index < token_count; index += 1usize) {
        u32 token = tokens[index];
        usize distance = (token >> 16usize) as usize;
        usize value = (token & 0xffffu32) as usize;
        if (distance == 0usize) {
            put_bits(writer, pending, literal_codes[value] as u32, literal_lengths[value] as u32);
        } else {
            usize slot = length_slot(value);
            usize symbol = 257usize + slot;
            put_bits(writer, pending, literal_codes[symbol] as u32, literal_lengths[symbol] as u32);
            u32 extra_width = LENGTH_EXTRA[slot] as u32;
            if (extra_width != 0u32) {
                put_bits(writer, pending, (value - (LENGTH_BASE[slot] as usize)) as u32, extra_width);
            }
            usize distance_code = distance_slot(distance);
            put_bits(writer, pending, distance_codes[distance_code] as u32, distance_lengths[distance_code] as u32);
            u32 distance_width = DISTANCE_EXTRA[distance_code] as u32;
            if (distance_width != 0u32) {
                put_bits(writer, pending, (distance - (DISTANCE_BASE[distance_code] as usize)) as u32, distance_width);
            }
        }
    }
    put_bits(writer, pending, literal_codes[256] as u32, literal_lengths[256] as u32);
}

/* Run-length description of the code lengths (RFC 1951 3.2.7): symbols and their extra
   values, plus the code-length alphabet frequencies. Returns the symbol count. */
protected usize describe_lengths(const u8[] literal_lengths, usize literal_count,
                                 const u8[] distance_lengths, usize distance_count,
                                 u8[] symbols, u8[] extras, u32[] code_freq) {
    u8[320] joined = {};
    usize total = literal_count + distance_count;
    for (usize index = 0usize; index < literal_count; index += 1usize) { joined[index] = literal_lengths[index]; }
    for (usize index = 0usize; index < distance_count; index += 1usize) {
        joined[literal_count + index] = distance_lengths[index];
    }
    for (usize index = 0usize; index < 19usize; index += 1usize) { code_freq[index] = 0u32; }
    usize produced = 0usize;
    usize position = 0usize;
    while (position < total) {
        u8 current = joined[position];
        usize run = 1usize;
        while ((position + run < total) && (joined[position + run] == current)) { run += 1usize; }
        position += run;
        if (current == 0) {
            while (run > 0usize) {
                if (run >= 11usize) {
                    usize taken = (run > 138usize) ? 138usize : run;
                    symbols[produced] = 18;
                    extras[produced] = (taken - 11usize) as u8;
                    code_freq[18] += 1u32;
                    produced += 1usize;
                    run -= taken;
                    continue;
                }
                if (run >= 3usize) {
                    symbols[produced] = 17;
                    extras[produced] = (run - 3usize) as u8;
                    code_freq[17] += 1u32;
                    produced += 1usize;
                    run = 0usize;
                    continue;
                }
                symbols[produced] = 0;
                extras[produced] = 0;
                code_freq[0] += 1u32;
                produced += 1usize;
                run -= 1usize;
            }
            continue;
        }
        symbols[produced] = current;
        extras[produced] = 0;
        code_freq[current as usize] += 1u32;
        produced += 1usize;
        run -= 1usize;
        while (run > 0usize) {
            if (run >= 3usize) {
                usize taken = (run > 6usize) ? 6usize : run;
                symbols[produced] = 16;
                extras[produced] = (taken - 3usize) as u8;
                code_freq[16] += 1u32;
                produced += 1usize;
                run -= taken;
                continue;
            }
            symbols[produced] = current;
            extras[produced] = 0;
            code_freq[current as usize] += 1u32;
            produced += 1usize;
            run -= 1usize;
        }
    }
    return produced;
}

/* Writes one block from the tokens: stored, fixed or dynamic, whichever is smallest. */
protected void emit_block(bit_writer* writer, u8[] pending, const u32[] tokens, usize token_count,
                          const u8[] block_bytes, const u32[] literal_freq, const u32[] distance_freq,
                          bool final, bool stored_only) {
    u32 final_bit = (final == true) ? 1u32 : 0u32;
    usize byte_count = len(block_bytes);
    usize stored_cost = byte_count * 8usize + STORED_OVERHEAD_BITS;
    bool use_stored = (stored_only == true) || (byte_count > 65535usize);

    u8[288] fixed_literal_lengths = {};
    u8[30] fixed_distance_lengths = {};
    u8[288] literal_lengths = {};
    u8[30] distance_lengths = {};
    u16[288] literal_codes = {};
    u16[30] distance_codes = {};
    u8[320] rle_symbols = {};
    u8[320] rle_extras = {};
    u32[19] code_freq = {};
    u8[19] code_lengths = {};
    u16[19] code_codes = {};
    usize rle_count = 0usize;
    usize literal_count = 257usize;
    usize distance_count = 1usize;
    usize code_count = 4usize;
    usize fixed_cost = 0usize;
    usize dynamic_cost = 0usize;
    if (use_stored == false) {
        for (usize index = 0usize; index < 144usize; index += 1usize) { fixed_literal_lengths[index] = 8; }
        for (usize index = 144usize; index < 256usize; index += 1usize) { fixed_literal_lengths[index] = 9; }
        for (usize index = 256usize; index < 280usize; index += 1usize) { fixed_literal_lengths[index] = 7; }
        for (usize index = 280usize; index < 288usize; index += 1usize) { fixed_literal_lengths[index] = 8; }
        for (usize index = 0usize; index < 30usize; index += 1usize) { fixed_distance_lengths[index] = 5; }
        const u8[] fixed_literal_view = &fixed_literal_lengths;
        const u8[] fixed_distance_view = &fixed_distance_lengths;
        fixed_cost = 3usize + token_cost(tokens, token_count, fixed_literal_view, fixed_distance_view);

        u32[286] literal_totals = {};
        for (usize index = 0usize; index < 286usize; index += 1usize) { literal_totals[index] = literal_freq[index]; }
        literal_totals[256] = 1u32;
        const u32[] literal_totals_view = &literal_totals;
        u8[] literal_lengths_view = &literal_lengths;
        build_lengths(literal_totals_view, literal_lengths_view, 286usize, MAX_BITS);
        u8[] distance_lengths_view = &distance_lengths;
        build_lengths(distance_freq, distance_lengths_view, 30usize, MAX_BITS);
        for (usize index = 257usize; index < 286usize; index += 1usize) {
            if (literal_lengths[index] != 0) { literal_count = index + 1usize; }
        }
        for (usize index = 1usize; index < 30usize; index += 1usize) {
            if (distance_lengths[index] != 0) { distance_count = index + 1usize; }
        }
        const u8[] literal_lengths_const = &literal_lengths;
        const u8[] distance_lengths_const = &distance_lengths;
        u8[] rle_symbols_view = &rle_symbols;
        u8[] rle_extras_view = &rle_extras;
        u32[] code_freq_view = &code_freq;
        rle_count = describe_lengths(literal_lengths_const, literal_count, distance_lengths_const,
                                     distance_count, rle_symbols_view, rle_extras_view, code_freq_view);
        const u32[] code_freq_const = &code_freq;
        u8[] code_lengths_view = &code_lengths;
        build_lengths(code_freq_const, code_lengths_view, 19usize, 7usize);
        for (usize index = 4usize; index < 19usize; index += 1usize) {
            usize symbol = CODE_LENGTH_ORDER[index] as usize;
            if (code_lengths[symbol] != 0) { code_count = index + 1usize; }
        }
        dynamic_cost = 3usize + 14usize + code_count * 3usize;
        for (usize index = 0usize; index < rle_count; index += 1usize) {
            usize symbol = rle_symbols[index] as usize;
            dynamic_cost += code_lengths[symbol] as usize;
            if (symbol == 16usize) { dynamic_cost += 2usize; }
            if (symbol == 17usize) { dynamic_cost += 3usize; }
            if (symbol == 18usize) { dynamic_cost += 7usize; }
        }
        dynamic_cost += token_cost(tokens, token_count, literal_lengths_const, distance_lengths_const);
        if ((stored_cost <= fixed_cost) && (stored_cost <= dynamic_cost)) { use_stored = true; }
    }
    if (use_stored == true) {
        put_bits(writer, pending, final_bit, 3u32);
        align_bits(writer, pending);
        put_bits(writer, pending, byte_count as u32, 16u32);
        put_bits(writer, pending, (byte_count as u32) ^ 0xffffu32, 16u32);
        /* The writer is byte aligned here, so the stored bytes are copied as they are. */
        copy_bytes(pending[writer->length..writer->length + byte_count], block_bytes);
        writer->length += byte_count;
        return;
    }
    if (fixed_cost <= dynamic_cost) {
        const u8[] fixed_literal_view = &fixed_literal_lengths;
        const u8[] fixed_distance_view = &fixed_distance_lengths;
        u16[] literal_codes_view = &literal_codes;
        u16[] distance_codes_view = &distance_codes;
        assign_codes(fixed_literal_view, literal_codes_view, 288usize);
        assign_codes(fixed_distance_view, distance_codes_view, 30usize);
        const u16[] literal_codes_const = &literal_codes;
        const u16[] distance_codes_const = &distance_codes;
        put_bits(writer, pending, final_bit | 2u32, 3u32);
        write_tokens(writer, pending, tokens, token_count, fixed_literal_view, literal_codes_const,
                     fixed_distance_view, distance_codes_const);
        return;
    }
    const u8[] literal_lengths_const = &literal_lengths;
    const u8[] distance_lengths_const = &distance_lengths;
    const u8[] code_lengths_const = &code_lengths;
    u16[] literal_codes_view = &literal_codes;
    u16[] distance_codes_view = &distance_codes;
    u16[] code_codes_view = &code_codes;
    assign_codes(literal_lengths_const, literal_codes_view, 288usize);
    assign_codes(distance_lengths_const, distance_codes_view, 30usize);
    assign_codes(code_lengths_const, code_codes_view, 19usize);
    put_bits(writer, pending, final_bit | 4u32, 3u32);
    put_bits(writer, pending, (literal_count - 257usize) as u32, 5u32);
    put_bits(writer, pending, (distance_count - 1usize) as u32, 5u32);
    put_bits(writer, pending, (code_count - 4usize) as u32, 4u32);
    for (usize index = 0usize; index < code_count; index += 1usize) {
        usize symbol = CODE_LENGTH_ORDER[index] as usize;
        put_bits(writer, pending, code_lengths[symbol] as u32, 3u32);
    }
    for (usize index = 0usize; index < rle_count; index += 1usize) {
        usize symbol = rle_symbols[index] as usize;
        put_bits(writer, pending, code_codes[symbol] as u32, code_lengths[symbol] as u32);
        if (symbol == 16usize) { put_bits(writer, pending, rle_extras[index] as u32, 2u32); }
        if (symbol == 17usize) { put_bits(writer, pending, rle_extras[index] as u32, 3u32); }
        if (symbol == 18usize) { put_bits(writer, pending, rle_extras[index] as u32, 7u32); }
    }
    const u16[] literal_codes_const = &literal_codes;
    const u16[] distance_codes_const = &distance_codes;
    write_tokens(writer, pending, tokens, token_count, literal_lengths_const, literal_codes_const,
                 distance_lengths_const, distance_codes_const);
}

/* The hash chains key a position by its three bytes: ((b0 << 10) ^ (b1 << 5) ^ b2) & mask. */

/* Longest match for the string at strstart among the chain starting at hash_head; the
   result is longer than prev_length or prev_length itself when nothing better exists. */
protected usize longest_match(const u8[] buffer, const u16[] prev, usize strstart, u16 hash_head,
                              usize lookahead, usize window_size, const level_config* config,
                              usize prev_length, usize* match_start) {
    usize best = prev_length;
    usize max_length = (lookahead < MAX_MATCH) ? lookahead : MAX_MATCH;
    usize nice = (config->nice < max_length) ? config->nice : max_length;
    usize limit = (strstart > window_size) ? strstart - window_size : 0usize;
    usize chain = config->chain;
    if (prev_length >= config->good) { chain >>= 2usize; }
    if ((strstart >= len(prev)) || (max_length < MIN_MATCH)) { return best; }
    const u8[] scan = buffer[strstart..strstart + max_length];
    if (len(scan) < MIN_MATCH) { return best; }
    u8 scan_first = scan[0];
    u8 scan_second = scan[1];
    u16 current = hash_head;
    while ((chain > 0usize) && (current != NIL)) {
        usize candidate = current as usize;
        if ((candidate < limit) || (candidate >= strstart)) { break; }
        if (best >= len(scan)) { break; }
        /* The candidate precedes strstart, so its string of the same length lies in the buffer. */
        const u8[] match = buffer[candidate..candidate + len(scan)];
        if ((best < len(match)) && (len(match) > 2usize) && (match[best] == scan[best]) &&
            (match[0] == scan_first) && (match[1] == scan_second)) {
            usize length = 2usize;
            while ((length < len(scan)) && (length < len(match)) && (scan[length] == match[length])) {
                length += 1usize;
            }
            if (length > best) {
                best = length;
                *match_start = candidate;
                if (best >= nice) { break; }
            }
        }
        current = prev[candidate];
        chain -= 1usize;
    }
    return best;
}

struct deflater {
    protected array<u8> buffer;
    protected array<u16> head;
    protected array<u16> prev;
    protected array<u32> tokens;
    protected array<u8> pending;
    protected u32[286] literal_freq;
    protected u32[32] distance_freq;
    protected format form;
    protected u8 level;
    protected usize window_size;
    protected usize hash_mask;
    protected usize hash_size;
    protected usize fill;
    protected usize strstart;
    protected usize block_start;
    protected usize token_count;
    protected usize pending_pos;
    protected usize pending_len;
    protected u64 bits;
    protected u32 bit_count;
    protected usize total_in;
    protected usize total_out;
    protected u32 check_a;
    protected u32 check_b;
    protected u8 stage;
    protected usize prev_length;
    protected usize prev_match;
    protected bool match_available;
    protected bool poisoned;
};

protected void reset_block_frequencies(deflater* this) {
    for (usize index = 0usize; index < 286usize; index += 1usize) { this->literal_freq[index] = 0u32; }
    for (usize index = 0usize; index < 32usize; index += 1usize) { this->distance_freq[index] = 0u32; }
}

deflater deflater::create(format form, u8 level, usize window_size)
    throws error, std.alloc::alloc_error {
    throw (level > 9) error { .code = error_code::invalid_level, .offset = 0usize };
    bool valid = window_is_valid(window_size);
    throw (valid == false) error { .code = error_code::invalid_window, .offset = 0usize };
    usize hash_bits = window_log2(window_size) + 2usize;
    if (hash_bits > 15usize) { hash_bits = 15usize; }
    usize hash_size = 1usize << hash_bits;
    array<u8> buffer = std.alloc::bytes(window_size * 2usize, 0u8);
    array<u16> head = std.array::filled(hash_size, NIL);
    array<u16> prev = std.array::filled(window_size * 2usize, NIL);
    array<u32> tokens = std.array::filled(window_size, 0u32);
    array<u8> pending = std.alloc::bytes(window_size + MAX_MATCH + PENDING_EXTRA, 0u8);
    deflater result = deflater {
        .buffer = move buffer,
        .head = move head,
        .prev = move prev,
        .tokens = move tokens,
        .pending = move pending,
        .form = form,
        .level = level,
        .window_size = window_size,
        .hash_mask = hash_size - 1usize,
        .hash_size = hash_size,
    };
    result.reset();
    return move result;
}

/* Returns the state to the start of a new stream; every allocation is kept. */
void deflater::reset(deflater* this) {
    reset_block_frequencies(this);
    this->fill = 0usize;
    this->strstart = 0usize;
    this->block_start = 0usize;
    this->token_count = 0usize;
    this->pending_pos = 0usize;
    this->pending_len = 0usize;
    this->bits = 0u64;
    this->bit_count = 0u32;
    this->total_in = 0usize;
    this->total_out = 0usize;
    this->check_a = 0u32;
    this->check_b = 0u32;
    if (this->form == format::rfc1950) { this->check_a = 1u32; }
    if (this->form == format::rfc1952) { this->check_a = 0xffffffffu32; }
    this->stage = 0;
    this->prev_length = MIN_MATCH - 1usize;
    this->prev_match = 0usize;
    this->match_available = false;
    this->poisoned = false;
    u16[] head = std.array::as_slice_mut(&this->head);
    for (usize index = 0usize; index < len(head); index += 1usize) { head[index] = NIL; }
}

usize deflater::consumed_bytes(const deflater* this) { return this->total_in; }
usize deflater::produced_bytes(const deflater* this) { return this->total_out; }

protected usize tokenized_end(const deflater* this) {
    if (this->match_available == true) { return this->strstart - 1usize; }
    return this->strstart;
}

/* Writes the pending tokens as one block and starts the next block at the tokenized end. */
protected void flush_block(deflater* this, bit_writer* writer, u8[] pending, const u32[] tokens,
                           const u8[] buffer, usize block_end, bool final) {
    const u32[] token_view = tokens[0usize..this->token_count];
    const u8[] block_view = buffer[this->block_start..block_end];
    const u32[] literal_view = &this->literal_freq;
    const u32[] distance_view = &this->distance_freq;
    emit_block(writer, pending, token_view, this->token_count, block_view, literal_view, distance_view,
               final, this->level == 0);
    this->token_count = 0usize;
    this->block_start = block_end;
    reset_block_frequencies(this);
}

/* Moves the upper window down once the buffer is full; positions in the chains follow. */
protected void slide_window(deflater* this, u8[] buffer, u16[] head, u16[] prev) {
    usize window_size = this->window_size;
    usize stop = this->fill;
    /* The upper part moves down; the destination precedes the source, so a forward copy is
       exact even where the two overlap. */
    usize target = 0usize;
    for (usize source = window_size; (source < stop) && (source < len(buffer)) && (target < source);
         source += 1usize) {
        buffer[target] = buffer[source];
        target += 1usize;
    }
    this->fill -= window_size;
    this->strstart -= window_size;
    this->block_start -= window_size;
    if (this->prev_match >= window_size) {
        this->prev_match -= window_size;
    } else {
        this->prev_length = MIN_MATCH - 1usize;
    }
    u16 shift = (window_size & 0xffffusize) as u16;
    for (usize index = 0usize; index < len(head); index += 1usize) {
        u16 value = head[index];
        head[index] = ((value != NIL) && (value >= shift)) ? (value - shift) as u16 : NIL;
    }
    usize lower = 0usize;
    for (usize upper = window_size; (upper < len(prev)) && (lower < upper) && (lower < window_size);
         upper += 1usize) {
        u16 value = prev[upper];
        prev[lower] = ((value != NIL) && (value >= shift)) ? (value - shift) as u16 : NIL;
        lower += 1usize;
    }
}

/* Inserts the positions first..last into the hash chains while three bytes remain before
   fill. The two earlier bytes of each hash come along from the position before, so a
   position loads one byte and no hash waits for the previous one. */
protected void insert_run(const u8[] buffer, u16[] head, u16[] prev, usize fill, usize mask,
                          usize first, usize last) {
    usize middle = first + 1usize;
    usize ahead = first + 2usize;
    if ((fill > len(buffer)) || (fill > len(prev)) || (mask >= len(head)) || (ahead >= fill) ||
        (middle >= ahead) || (first >= middle)) {
        return;
    }
    usize earlier = buffer[first] as usize;
    usize previous = buffer[middle] as usize;
    usize position = first;
    while ((ahead < fill) && (position < ahead) && (position <= last)) {
        usize current = buffer[ahead] as usize;
        usize hash = ((earlier << 10usize) ^ (previous << 5usize) ^ current) & mask;
        prev[position] = head[hash & mask];
        head[hash & mask] = (position & 0xffffusize) as u16;
        earlier = previous;
        previous = current;
        position += 1usize;
        ahead += 1usize;
    }
}

/* Tokenizes from strstart while the lookahead allows: MIN_LOOKAHEAD bytes, or the rest of the
   buffer when it is full or the input is drained. Greedy matching below level 4; lazy
   matching from level 4, where a match is emitted only when the next position does not
   start a longer one and otherwise the current byte becomes a literal. True when the block
   is full and must be written first. */
protected bool tokenize(deflater* this, const level_config* config, const u8[] buffer, u16[] head,
                        u16[] prev, u32[] tokens, bool buffer_full, bool draining) {
    usize fill = this->fill;
    usize mask = this->hash_mask;
    if ((fill > len(buffer)) || (fill > len(prev)) || (mask >= len(head))) { return false; }
    usize window_size = this->window_size;
    usize block_start = this->block_start;
    usize strstart = this->strstart;
    usize token_count = this->token_count;
    usize prev_length = this->prev_length;
    usize prev_match = this->prev_match;
    bool match_available = this->match_available;
    bool lazy = config->lazy_matching;
    bool chained = config->chain != 0usize;
    usize lazy_limit = config->lazy;
    bool full = false;
    u8 held = 0u8;
    if ((match_available == true) && (strstart != 0usize)) { held = buffer[strstart - 1usize]; }
    while (strstart < fill) {
        usize lookahead = fill - strstart;
        if ((lookahead < MIN_LOOKAHEAD) && (buffer_full == false) && (draining == false)) { break; }
        if (token_count >= len(tokens)) {
            full = true;
            break;
        }
        u16 hash_head = NIL;
        usize middle = strstart + 1usize;
        usize ahead = strstart + 2usize;
        if ((chained == true) && (ahead < fill) && (middle < ahead)) {
            usize hash = (((buffer[strstart] as usize) << 10usize) ^ ((buffer[middle] as usize) << 5usize) ^
                          (buffer[ahead] as usize)) & mask;
            hash_head = head[hash & mask];
            prev[strstart] = hash_head;
            head[hash & mask] = (strstart & 0xffffusize) as u16;
        }
        if (lazy == false) {
            usize match_length = MIN_MATCH - 1usize;
            usize match_start = 0usize;
            if (hash_head != NIL) {
                match_length = longest_match(buffer, prev, strstart, hash_head, lookahead, window_size,
                                             config, MIN_MATCH - 1usize, &match_start);
            }
            if (match_length < MIN_MATCH) {
                u8 literal = buffer[strstart];
                tokens[token_count] = literal as u32;
                token_count += 1usize;
                this->literal_freq[literal as usize] += 1u32;
                strstart += 1usize;
            } else {
                usize distance = strstart - match_start;
                tokens[token_count] = ((distance as u32) << 16usize) | (match_length as u32);
                token_count += 1usize;
                this->literal_freq[257usize + length_slot(match_length)] += 1u32;
                this->distance_freq[distance_slot(distance) & 31usize] += 1u32;
                if ((match_length <= lazy_limit) && (chained == true)) {
                    insert_run(buffer, head, prev, fill, mask, strstart + 1usize,
                               strstart + match_length - 1usize);
                }
                strstart += match_length;
            }
        } else {
            usize match_length = prev_length;
            usize match_start = prev_match;
            if ((hash_head != NIL) && (prev_length < lazy_limit)) {
                usize found_start = 0usize;
                usize found = longest_match(buffer, prev, strstart, hash_head, lookahead, window_size,
                                            config, prev_length, &found_start);
                bool too_far = (found == MIN_MATCH) && (strstart - found_start > 4096usize);
                if ((found > prev_length) && (too_far == false)) {
                    match_length = found;
                    match_start = found_start;
                }
            }
            if ((prev_length >= MIN_MATCH) && (match_length <= prev_length)) {
                usize distance = (strstart - 1usize) - prev_match;
                tokens[token_count] = ((distance as u32) << 16usize) | (prev_length as u32);
                token_count += 1usize;
                this->literal_freq[257usize + length_slot(prev_length)] += 1u32;
                this->distance_freq[distance_slot(distance) & 31usize] += 1u32;
                if (chained == true) {
                    insert_run(buffer, head, prev, fill, mask, strstart + 1usize,
                               strstart + prev_length - 2usize);
                }
                strstart += prev_length - 1usize;
                match_available = false;
                prev_length = MIN_MATCH - 1usize;
                prev_match = 0usize;
            } else {
                if (match_available == true) {
                    tokens[token_count] = held as u32;
                    token_count += 1usize;
                    this->literal_freq[held as usize] += 1u32;
                }
                match_available = true;
                held = buffer[strstart];
                prev_length = match_length;
                prev_match = match_start;
                strstart += 1usize;
            }
        }
        usize tokenized = strstart;
        if (match_available == true) { tokenized = strstart - 1usize; }
        if ((token_count >= window_size) || (tokenized - block_start >= window_size)) {
            full = true;
            break;
        }
    }
    this->strstart = strstart;
    this->token_count = token_count;
    this->prev_length = prev_length;
    this->prev_match = prev_match;
    this->match_available = match_available;
    return full;
}

protected void write_stream_header(const deflater* this, bit_writer* writer, u8[] pending) {
    if (this->form == format::rfc1950) {
        u32 cmf = 8u32 | (((window_log2(this->window_size) - 8usize) as u32) << 4usize);
        u32 flevel = 3u32;
        if (this->level < 2) { flevel = 0u32; }
        if ((this->level >= 2) && (this->level < 6)) { flevel = 1u32; }
        if (this->level == 6) { flevel = 2u32; }
        u32 flg = flevel << 6usize;
        u32 remainder = (cmf * 256u32 + flg) % 31u32;
        if (remainder != 0u32) { flg |= 31u32 - remainder; }
        put_bits(writer, pending, cmf, 8u32);
        put_bits(writer, pending, flg, 8u32);
    }
    if (this->form == format::rfc1952) {
        u32 xfl = 0u32;
        if (this->level == 9) { xfl = 2u32; }
        if (this->level == 1) { xfl = 4u32; }
        put_bits(writer, pending, 0x1fu32, 8u32);
        put_bits(writer, pending, 0x8bu32, 8u32);
        put_bits(writer, pending, 8u32, 8u32);
        put_bits(writer, pending, 0u32, 8u32);
        put_bits(writer, pending, 0u32, 16u32);
        put_bits(writer, pending, 0u32, 16u32);
        put_bits(writer, pending, xfl, 8u32);
        put_bits(writer, pending, 255u32, 8u32);
    }
}

protected void write_stream_trailer(const deflater* this, bit_writer* writer, u8[] pending) {
    align_bits(writer, pending);
    if (this->form == format::rfc1950) {
        put_bits(writer, pending, this->check_b >> 8usize, 8u32);
        put_bits(writer, pending, this->check_b & 0xffu32, 8u32);
        put_bits(writer, pending, this->check_a >> 8usize, 8u32);
        put_bits(writer, pending, this->check_a & 0xffu32, 8u32);
    }
    if (this->form == format::rfc1952) {
        u32 crc = this->check_a ^ 0xffffffffu32;
        u32 size = (this->total_in & 0xffffffffusize) as u32;
        put_bits(writer, pending, crc & 0xffffu32, 16u32);
        put_bits(writer, pending, crc >> 16usize, 16u32);
        put_bits(writer, pending, size & 0xffffu32, 16u32);
        put_bits(writer, pending, size >> 16usize, 16u32);
    }
}

/* Copies input into the lookahead and checksums it; returns the number of bytes taken. */
protected usize intake(deflater* this, u8[] buffer, const u8[] input, usize pos) {
    usize capacity = this->window_size * 2usize;
    if ((this->fill >= capacity) || (pos >= len(input))) { return 0usize; }
    usize room = capacity - this->fill;
    usize available = len(input) - pos;
    usize count = (room < available) ? room : available;
    const u8[] taken = input[pos..pos + count];
    copy_bytes(buffer[this->fill..this->fill + count], taken);
    checksum_pair next = checksum_update(this->form, this->check_a, this->check_b, taken);
    this->check_a = next.a;
    this->check_b = next.b;
    this->fill += count;
    this->total_in += count;
    return count;
}

progress deflater::deflate(deflater* this, const u8[] input, u8[] output, flush mode) throws error {
    throw (this->poisoned == true) error { .code = error_code::poisoned, .offset = this->total_in };
    throw (this->stage == 3) error { .code = error_code::finished, .offset = this->total_in };
    level_config config = config_for(this->level);
    const level_config* settings = &config;
    usize window_size = this->window_size;
    usize capacity = window_size * 2usize;
    usize input_size = len(input);
    usize output_size = len(output);
    usize pos = 0usize;
    usize out_pos = 0usize;
    state result = state::need_input;
    bool stop = false;
    bool flushed = false;
    bit_writer writer = bit_writer { .acc = this->bits, .count = this->bit_count, .length = this->pending_len };

    array<u8> buffer_owner = core::replace(&this->buffer, std.array::create::<u8>());
    array<u16> head_owner = core::replace(&this->head, std.array::create::<u16>());
    array<u16> prev_owner = core::replace(&this->prev, std.array::create::<u16>());
    array<u32> tokens_owner = core::replace(&this->tokens, std.array::create::<u32>());
    array<u8> pending_owner = core::replace(&this->pending, std.array::create::<u8>());
    u8[] buffer = std.array::as_slice_mut(&buffer_owner);
    u16[] head = std.array::as_slice_mut(&head_owner);
    u16[] prev = std.array::as_slice_mut(&prev_owner);
    u32[] tokens = std.array::as_slice_mut(&tokens_owner);
    u8[] pending = std.array::as_slice_mut(&pending_owner);
    while (stop == false) {
        /* 1. Drain staged output before anything else is produced. */
        if ((this->pending_pos < writer.length) && (out_pos < output_size)) {
            usize waiting = writer.length - this->pending_pos;
            usize room = output_size - out_pos;
            usize count = (waiting < room) ? waiting : room;
            copy_bytes(output[out_pos..out_pos + count],
                       pending[this->pending_pos..this->pending_pos + count]);
            out_pos += count;
            this->pending_pos += count;
        }
        if (this->pending_pos < writer.length) {
            result = state::need_output;
            stop = true;
            break;
        }
        this->pending_pos = 0usize;
        writer.length = 0usize;
        if (this->stage == 2) {
            this->stage = 3;
            result = state::end;
            stop = true;
            break;
        }
        /* 2. Stream header. */
        if (this->stage == 0) {
            write_stream_header(this, &writer, pending);
            this->stage = 1;
            continue;
        }
        /* 3. Slide once the buffer is full and the history exceeds one window; the pending
           block is written first so that its stored form keeps its bytes. */
        if ((this->fill == capacity) && (this->strstart >= window_size)) {
            if (this->token_count != 0usize) {
                usize pending_end = tokenized_end(this);
                flush_block(this, &writer, pending, tokens, buffer, pending_end, false);
                continue;
            }
            slide_window(this, buffer, head, prev);
        }
        /* 4. Intake. */
        pos += intake(this, buffer, input, pos);
        /* 5. Tokenize while the lookahead is long enough, while the buffer is full but cannot
           slide yet (small windows), or to the end when flushing. */
        bool input_exhausted = pos >= input_size;
        bool buffer_full = this->fill == capacity;
        bool draining = (mode != flush::none) && (input_exhausted == true);
        bool block_full = tokenize(this, settings, buffer, head, prev, tokens, buffer_full, draining);
        if (block_full == true) {
            usize block_end = tokenized_end(this);
            flush_block(this, &writer, pending, tokens, buffer, block_end, false);
            continue;
        }
        if (input_exhausted == false) { continue; }
        if (mode == flush::none) {
            stop = true;
            break;
        }
        if (flushed == true) {
            if (mode == flush::sync) { stop = true; }
            continue;
        }
        /* 6. Flush: the pending literal of lazy matching, the block, then the terminator. */
        if (this->match_available == true) {
            u8 last_literal = buffer[this->strstart - 1usize];
            tokens[this->token_count] = last_literal as u32;
            this->token_count += 1usize;
            this->literal_freq[last_literal as usize] += 1u32;
            this->match_available = false;
            this->prev_length = MIN_MATCH - 1usize;
        }
        bool final = mode == flush::finish;
        if ((this->token_count != 0usize) || (final == true)) {
            usize final_end = this->strstart;
            flush_block(this, &writer, pending, tokens, buffer, final_end, final);
        }
        if (final == true) {
            write_stream_trailer(this, &writer, pending);
            this->stage = 2;
        } else {
            put_bits(&writer, pending, 0u32, 3u32);
            align_bits(&writer, pending);
            put_bits(&writer, pending, 0u32, 16u32);
            put_bits(&writer, pending, 0xffffu32, 16u32);
        }
        flushed = true;
    }
    this->buffer = move buffer_owner;
    this->head = move head_owner;
    this->prev = move prev_owner;
    this->tokens = move tokens_owner;
    this->pending = move pending_owner;
    this->bits = writer.acc;
    this->bit_count = writer.count;
    this->pending_len = writer.length;
    this->total_out += out_pos;
    return progress { .consumed = pos, .produced = out_pos, .state = result };
}

/* R-SLIB-DEFLATE-0005: one-shot compression of one complete input, written straight into the
   result. */
array<u8> deflate(const u8[] input, format form, u8 level) throws error, std.alloc::alloc_error {
    deflater engine = deflater::create(form, level, WINDOW_MAX);
    usize input_size = len(input);
    array<u8> result = std.alloc::bytes(input_size / 2usize + ONE_SHOT_CHUNK, 0u8);
    usize produced = 0usize;
    usize consumed = 0usize;
    while (true) {
        u8[] room = std.array::as_slice_mut(&result);
        u8[] out_view = room[produced..len(room)];
        const u8[] rest = input[consumed..input_size];
        progress step = engine.deflate(rest, out_view, flush::finish);
        consumed += step.consumed;
        produced += step.produced;
        if (step.state == state::end) { return exact_output(move result, produced); }
        if (step.state == state::need_output) { grow_output(&result, produced, produced + ONE_SHOT_CHUNK); }
    }
}
