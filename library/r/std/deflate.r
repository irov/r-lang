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
protected const u32 CRC_POLYNOMIAL = 0xedb88320u32;

/* Canonical Huffman decoding table: codes per length and symbols ordered by code. */
protected struct huffman {
    u16[16] count;
    u16[288] symbols;
    usize symbol_count;
};

/* One decoded symbol; status 0 = decoded, 1 = more bits are required, 2 = invalid code. */
protected struct decoded {
    u16 symbol;
    u32 used;
    u8 status;
};

/* Output position shared by the emitting stages of the inflater. */
protected struct cursor {
    usize out_pos;
    usize window_pos;
    usize window_fill;
    usize total_out;
    u32 check_a;
    u32 check_b;
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

protected u32 crc32_update(u32 crc, u8 value) {
    u32 current = crc ^ (value as u32);
    for (usize bit = 0usize; bit < 8usize; bit += 1usize) {
        u32 mask = (current & 1u32) * CRC_POLYNOMIAL;
        current = (current >> 1usize) ^ mask;
    }
    return current;
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
            usize slot = length as usize;
            usize destination = offsets[slot] as usize;
            table->symbols[destination] = symbol as u16;
            offsets[slot] += 1;
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

/* Decodes one code from the low bits of the accumulator without consuming them. */
protected decoded decode_symbol(const huffman* table, u64 bits, u32 available) {
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

protected void emit_byte(cursor* at, u8 value, u8[] output, u8[] window, usize window_mask,
                         format form) {
    output[at->out_pos] = value;
    at->out_pos += 1usize;
    window[at->window_pos] = value;
    at->window_pos = (at->window_pos + 1usize) & window_mask;
    if (at->window_fill <= window_mask) { at->window_fill += 1usize; }
    at->total_out += 1usize;
    if (form == format::rfc1950) {
        at->check_a = (at->check_a + (value as u32)) % ADLER_MODULUS;
        at->check_b = (at->check_b + at->check_a) % ADLER_MODULUS;
    }
    if (form == format::rfc1952) {
        at->check_a = crc32_update(at->check_a, value);
    }
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
        huffman literals = {};
        bool literals_ok = build(&literals, literal_view, true);
        if (literals_ok == false) { return STEP_CORRUPT; }
        this->literals = literals;
        bool has_distance = false;
        for (usize index = this->literal_count; index < total; index += 1usize) {
            if (lengths_copy[index] != 0) { has_distance = true; }
        }
        if (has_distance == true) {
            const u8[] distance_view = lengths_copy[this->literal_count..total];
            huffman distances = {};
            bool distances_ok = build(&distances, distance_view, true);
            if (distances_ok == false) { return STEP_CORRUPT; }
            this->distances = distances;
        } else {
            for (usize slot = 0usize; slot < 16usize; slot += 1usize) { this->distances.count[slot] = 0; }
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
protected u8 decode_next(inflater* this, u64* bits, u32* bit_count, cursor* at, u8[] output,
                         u8[] window, const u16[] length_base, const u8[] length_extra,
                         const u16[] distance_base, const u8[] distance_extra) {
    if (at->out_pos >= len(output)) { return STEP_NEED_OUTPUT; }
    decoded code = decode_symbol(&this->literals, *bits, *bit_count);
    if (code.status == 1) { return STEP_NEED_INPUT; }
    if (code.status == 2) { return STEP_CORRUPT; }
    if (code.symbol < 256) {
        if (at->total_out >= this->output_limit) { return STEP_LIMIT; }
        take_bits(bits, bit_count, code.used);
        emit_byte(at, code.symbol as u8, output, window, this->window_mask, this->form);
        return STEP_CONTINUE;
    }
    if (code.symbol == 256) {
        take_bits(bits, bit_count, code.used);
        return STEP_DONE;
    }
    if (code.symbol > 285) { return STEP_CORRUPT; }
    usize length_index_slot = (code.symbol - 257) as usize;
    u32 length_width = length_extra[length_index_slot] as u32;
    u32 used = code.used + length_width;
    if (*bit_count < used) { return STEP_NEED_INPUT; }
    decoded distance_code = decode_symbol(&this->distances, *bits >> (used as usize), *bit_count - used);
    if (distance_code.status == 1) { return STEP_NEED_INPUT; }
    if (distance_code.status == 2) { return STEP_CORRUPT; }
    if (distance_code.symbol >= 30) { return STEP_CORRUPT; }
    usize distance_index_slot = distance_code.symbol as usize;
    u32 distance_width = distance_extra[distance_index_slot] as u32;
    u32 total_used = used + distance_code.used + distance_width;
    if (*bit_count < total_used) { return STEP_NEED_INPUT; }
    u32 length_delta = extract(*bits, code.used, length_width);
    u32 distance_delta = extract(*bits, used + distance_code.used, distance_width);
    usize match_length = (length_base[length_index_slot] as usize) + (length_delta as usize);
    usize distance = (distance_base[distance_index_slot] as usize) + (distance_delta as usize);
    if ((distance > at->window_fill) || (distance > this->window_size)) { return STEP_CORRUPT; }
    take_bits(bits, bit_count, total_used);
    this->remaining = match_length;
    this->copy_distance = distance;
    return STEP_CONTINUE;
}

/* Stage 13: copies the match from the window until it is complete or the output is full. */
protected u8 copy_match(inflater* this, cursor* at, u8[] output, u8[] window) {
    usize output_size = len(output);
    while (this->remaining != 0usize) {
        if (at->out_pos >= output_size) { return STEP_NEED_OUTPUT; }
        if (at->total_out >= this->output_limit) { return STEP_LIMIT; }
        usize source = (at->window_pos + this->window_size - this->copy_distance) & this->window_mask;
        u8 byte = window[source];
        emit_byte(at, byte, output, window, this->window_mask, this->form);
        this->remaining -= 1usize;
    }
    return STEP_DONE;
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
    u16[29] length_base = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
    };
    u8[29] length_extra = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
    };
    u16[30] distance_base = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
    };
    u8[30] distance_extra = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
    };
    u8[19] order = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    const u16[] length_base_view = &length_base;
    const u8[] length_extra_view = &length_extra;
    const u16[] distance_base_view = &distance_base;
    const u8[] distance_extra_view = &distance_extra;

    array<u8> window_owner = core::replace(&this->window, std.array::create::<u8>());
    u8[] window = std.array::as_slice_mut(&window_owner);
    cursor at = cursor {
        .out_pos = 0usize,
        .window_pos = this->window_pos,
        .window_fill = this->window_fill,
        .total_out = this->total_out,
        .check_a = this->check_a,
        .check_b = this->check_b,
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
                huffman fixed_literals = {};
                huffman fixed_distances = {};
                bool built = build_fixed(&fixed_literals, &fixed_distances);
                if (built == false) { outcome = STEP_CORRUPT; break; }
                this->literals = fixed_literals;
                this->distances = fixed_distances;
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
            if (bit_count < 8u32) { outcome = STEP_NEED_INPUT; break; }
            if (at.total_out >= this->output_limit) { outcome = STEP_LIMIT; break; }
            u8 stored_byte = extract(bits, 0u32, 8u32) as u8;
            take_bits(&bits, &bit_count, 8u32);
            emit_byte(&at, stored_byte, output, window, this->window_mask, form);
            this->remaining -= 1usize;
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
                huffman code_table = {};
                bool built = build(&code_table, code_view, false);
                if (built == false) { outcome = STEP_CORRUPT; break; }
                this->code_table = code_table;
                this->length_index = 0usize;
                this->previous_length = 0;
                for (usize index = 0usize; index < 320usize; index += 1usize) { this->lengths[index] = 0; }
                stage = 11;
                break;
            }
            if (bit_count < 3u32) { outcome = STEP_NEED_INPUT; break; }
            usize slot = order[this->length_index] as usize;
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
            outcome = decode_next(this, &bits, &bit_count, &at, output, window, length_base_view,
                                  length_extra_view, distance_base_view, distance_extra_view);
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

protected void append_bytes(array<u8>* target, const u8[] source, usize count)
    throws std.alloc::alloc_error {
    std.array::reserve(target, count);
    for (usize index = 0usize; index < count; index += 1usize) {
        bool failed = false;
        try { std.array::push(target, source[index]); }
        catch (std.array::push_error<u8> failure) { failed = true; }
        if (failed == true) { panic("reserved byte insertion failed"); }
    }
}

/* R-SLIB-DEFLATE-0005: one-shot decompression of one complete stream. */
array<u8> inflate(const u8[] input, format form, usize output_limit)
    throws error, std.alloc::alloc_error {
    inflater engine = inflater::create(form, WINDOW_MAX, output_limit);
    array<u8> result = std.array::create::<u8>();
    array<u8> scratch = std.alloc::bytes(ONE_SHOT_CHUNK, 0u8);
    usize consumed = 0usize;
    usize input_size = len(input);
    while (true) {
        u8[] out_view = std.array::as_slice_mut(&scratch);
        const u8[] rest = input[consumed..input_size];
        progress step = engine.inflate(rest, out_view);
        consumed += step.consumed;
        append_bytes(&result, out_view, step.produced);
        if (step.state == state::end) {
            throw (consumed != input_size) error { .code = error_code::corrupt_stream, .offset = consumed };
            return move result;
        }
        throw (step.state == state::need_input) error { .code = error_code::corrupt_stream, .offset = consumed };
    }
}

/* R-SLIB-DEFLATE-0004: the deflater. LZ77 over a sliding buffer of twice the window with
   hash chains, greedy matching below level 4 and lazy matching from level 4, blocks chosen
   by their exact bit cost among stored, fixed and dynamic Huffman coding. */
protected const usize MIN_MATCH = 3usize;
protected const usize MAX_MATCH = 258usize;
protected const usize MIN_LOOKAHEAD = 262usize;
protected const usize PENDING_EXTRA = 1024usize;
protected const usize STORED_OVERHEAD_BITS = 40usize;
protected const u32 NIL = 0xffffffffu32;

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

protected u32 reverse_bits(u32 code, usize width) {
    u32 result = 0u32;
    u32 rest = code;
    for (usize index = 0usize; index < width; index += 1usize) {
        result = (result << 1usize) | (rest & 1u32);
        rest >>= 1usize;
    }
    return result;
}

protected usize length_slot(usize length) {
    u16[29] base = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
    };
    usize slot = 28usize;
    while (slot > 0usize) {
        if (length >= (base[slot] as usize)) { return slot; }
        slot -= 1usize;
    }
    return 0usize;
}

protected usize distance_slot(usize distance) {
    u16[30] base = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
    };
    usize slot = 29usize;
    while (slot > 0usize) {
        if (distance >= (base[slot] as usize)) { return slot; }
        slot -= 1usize;
    }
    return 0usize;
}

/* Length-limited canonical code lengths from frequencies (zlib's overflow adjustment); at
   least two codes are produced so that the block can always be written. */
protected void build_lengths(const u32[] freq, u8[] lengths, usize count, usize max_bits) {
    u32[576] weight = {};
    u16[576] parent = {};
    u8[576] active = {};
    usize leaves = 0usize;
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
    usize node_count = count;
    usize remaining = leaves;
    while (remaining > 1usize) {
        usize first = node_count;
        usize second = node_count;
        for (usize node = 0usize; node < node_count; node += 1usize) {
            if (active[node] == 0) {
                second = second;
            } else {
                if ((first == node_count) || (weight[node] < weight[first])) {
                    second = first;
                    first = node;
                } else {
                    if ((second == node_count) || (weight[node] < weight[second])) { second = node; }
                }
            }
        }
        active[first] = 0;
        active[second] = 0;
        weight[node_count] = weight[first] + weight[second];
        parent[first] = node_count as u16;
        parent[second] = node_count as u16;
        active[node_count] = 1;
        node_count += 1usize;
        remaining -= 1usize;
    }
    usize root = node_count - 1usize;
    u16[16] bl_count = {};
    usize overflow = 0usize;
    u16[288] leaf_order = {};
    usize leaf_count = 0usize;
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
            /* Insertion by ascending frequency: the least frequent leaves take the longest codes. */
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
    if (overflow == 0usize) { return; }
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
            codes[symbol] = (reverse_bits(next_code[length] & 0x7fffu32, length) & 0xffffu32) as u16;
            next_code[length] += 1u32;
        }
    }
}

protected usize token_cost(const u32[] tokens, usize token_count, const u8[] literal_lengths,
                           const u8[] distance_lengths) {
    u8[29] length_extra = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
    };
    u8[30] distance_extra = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
    };
    usize bits = literal_lengths[256] as usize;
    for (usize index = 0usize; index < token_count; index += 1usize) {
        u32 token = tokens[index];
        usize distance = (token >> 16usize) as usize;
        usize value = (token & 0xffffu32) as usize;
        if (distance == 0usize) {
            bits += literal_lengths[value] as usize;
        } else {
            usize slot = length_slot(value);
            bits += (literal_lengths[257usize + slot] as usize) + (length_extra[slot] as usize);
            usize distance_code = distance_slot(distance);
            bits += (distance_lengths[distance_code] as usize) + (distance_extra[distance_code] as usize);
        }
    }
    return bits;
}

protected void write_tokens(bit_writer* writer, u8[] pending, const u32[] tokens, usize token_count,
                            const u8[] literal_lengths, const u16[] literal_codes,
                            const u8[] distance_lengths, const u16[] distance_codes) {
    u16[29] length_base = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258,
    };
    u8[29] length_extra = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0,
    };
    u16[30] distance_base = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577,
    };
    u8[30] distance_extra = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13,
    };
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
            u32 extra_width = length_extra[slot] as u32;
            if (extra_width != 0u32) {
                put_bits(writer, pending, (value - (length_base[slot] as usize)) as u32, extra_width);
            }
            usize distance_code = distance_slot(distance);
            put_bits(writer, pending, distance_codes[distance_code] as u32, distance_lengths[distance_code] as u32);
            u32 distance_width = distance_extra[distance_code] as u32;
            if (distance_width != 0u32) {
                put_bits(writer, pending, (distance - (distance_base[distance_code] as usize)) as u32, distance_width);
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
    u8[19] order = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
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
            usize symbol = order[index] as usize;
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
        for (usize index = 0usize; index < byte_count; index += 1usize) {
            put_bits(writer, pending, block_bytes[index] as u32, 8u32);
        }
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
        usize symbol = order[index] as usize;
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

protected usize hash_bytes(const u8[] buffer, usize position, usize hash_mask) {
    usize first = buffer[position] as usize;
    usize second = buffer[position + 1usize] as usize;
    usize third = buffer[position + 2usize] as usize;
    return ((first << 10usize) ^ (second << 5usize) ^ third) & hash_mask;
}

/* Longest match for the string at strstart among the chain starting at hash_head; the
   result is longer than prev_length or prev_length itself when nothing better exists. */
protected usize longest_match(const u8[] buffer, const u32[] prev, usize strstart, u32 hash_head,
                              usize lookahead, usize window_size, const level_config* config,
                              usize prev_length, usize* match_start) {
    usize best = prev_length;
    usize max_length = (lookahead < MAX_MATCH) ? lookahead : MAX_MATCH;
    usize nice = (config->nice < max_length) ? config->nice : max_length;
    usize limit = (strstart > window_size) ? strstart - window_size : 0usize;
    usize chain = config->chain;
    if (prev_length >= config->good) { chain >>= 2usize; }
    u32 current = hash_head;
    while ((chain > 0usize) && (current != NIL)) {
        usize candidate = current as usize;
        if ((candidate < limit) || (candidate >= strstart)) { break; }
        if (best >= max_length) { break; }
        if ((buffer[candidate + best] == buffer[strstart + best]) &&
            (buffer[candidate] == buffer[strstart]) &&
            (buffer[candidate + 1usize] == buffer[strstart + 1usize])) {
            usize length = 2usize;
            while ((length < max_length) && (buffer[candidate + length] == buffer[strstart + length])) {
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
    protected array<u32> head;
    protected array<u32> prev;
    protected array<u32> tokens;
    protected array<u8> pending;
    protected u32[286] literal_freq;
    protected u32[30] distance_freq;
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

protected array<u32> filled_u32(usize count, u32 value) throws std.alloc::alloc_error {
    array<u32> result = std.array::with_capacity::<u32>(count);
    for (usize index = 0usize; index < count; index += 1usize) {
        bool failed = false;
        try { std.array::push(&result, value); }
        catch (std.array::push_error<u32> failure) { failed = true; }
        if (failed == true) { panic("reserved table insertion failed"); }
    }
    return move result;
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
    array<u32> head = filled_u32(hash_size, NIL);
    array<u32> prev = filled_u32(window_size * 2usize, NIL);
    array<u32> tokens = filled_u32(window_size, 0u32);
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
    for (usize index = 0usize; index < 286usize; index += 1usize) { this->literal_freq[index] = 0u32; }
    for (usize index = 0usize; index < 30usize; index += 1usize) { this->distance_freq[index] = 0u32; }
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
    u32[] head = std.array::as_slice_mut(&this->head);
    for (usize index = 0usize; index < len(head); index += 1usize) { head[index] = NIL; }
}

usize deflater::consumed_bytes(const deflater* this) { return this->total_in; }
usize deflater::produced_bytes(const deflater* this) { return this->total_out; }

protected void reset_block_frequencies(deflater* this) {
    for (usize index = 0usize; index < 286usize; index += 1usize) { this->literal_freq[index] = 0u32; }
    for (usize index = 0usize; index < 30usize; index += 1usize) { this->distance_freq[index] = 0u32; }
}

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
protected void slide_window(deflater* this, u8[] buffer, u32[] head, u32[] prev) {
    usize window_size = this->window_size;
    /* The upper part moves down; the destination precedes the source, so a forward copy is
       exact even where the two overlap. */
    usize moving = this->fill - window_size;
    for (usize index = 0usize; index < moving; index += 1usize) {
        buffer[index] = buffer[window_size + index];
    }
    this->fill -= window_size;
    this->strstart -= window_size;
    this->block_start -= window_size;
    if (this->prev_match >= window_size) {
        this->prev_match -= window_size;
    } else {
        this->prev_length = MIN_MATCH - 1usize;
    }
    u32 shift = window_size as u32;
    for (usize index = 0usize; index < this->hash_size; index += 1usize) {
        u32 value = head[index];
        head[index] = ((value != NIL) && (value >= shift)) ? value - shift : NIL;
    }
    for (usize index = 0usize; index < window_size; index += 1usize) {
        u32 value = prev[index + window_size];
        prev[index] = ((value != NIL) && (value >= shift)) ? value - shift : NIL;
    }
}

protected void insert_string(deflater* this, const u8[] buffer, u32[] head, u32[] prev, usize position) {
    usize hash = hash_bytes(buffer, position, this->hash_mask);
    u32 chained = head[hash];
    prev[position] = chained;
    head[hash] = position as u32;
}

protected void push_literal(deflater* this, u32[] tokens, u8 literal) {
    tokens[this->token_count] = literal as u32;
    this->token_count += 1usize;
    this->literal_freq[literal as usize] += 1u32;
}

protected void push_match(deflater* this, u32[] tokens, usize length, usize distance) {
    tokens[this->token_count] = ((distance as u32) << 16usize) | (length as u32);
    this->token_count += 1usize;
    this->literal_freq[257usize + length_slot(length)] += 1u32;
    this->distance_freq[distance_slot(distance)] += 1u32;
}

/* Greedy matching (levels 1 through 3) and level 0: one token per call. */
protected void tokenize_greedy(deflater* this, const level_config* config, const u8[] buffer,
                               u32[] head, u32[] prev, u32[] tokens, u32 hash_head, usize* lookahead) {
    usize strstart = this->strstart;
    usize match_length = MIN_MATCH - 1usize;
    usize match_start = 0usize;
    if (hash_head != NIL) {
        match_length = longest_match(buffer, prev, strstart, hash_head, *lookahead, this->window_size,
                                     config, MIN_MATCH - 1usize, &match_start);
    }
    if (match_length < MIN_MATCH) {
        u8 literal = buffer[strstart];
        push_literal(this, tokens, literal);
        this->strstart = strstart + 1usize;
        *lookahead -= 1usize;
        return;
    }
    push_match(this, tokens, match_length, strstart - match_start);
    *lookahead -= match_length;
    if ((match_length > config->lazy) || (config->chain == 0usize)) {
        this->strstart = strstart + match_length;
        return;
    }
    usize insert_limit = this->fill - MIN_MATCH;
    for (usize inserted = 1usize; inserted < match_length; inserted += 1usize) {
        strstart += 1usize;
        if (strstart <= insert_limit) { insert_string(this, buffer, head, prev, strstart); }
    }
    this->strstart = strstart + 1usize;
}

/* Lazy matching (levels 4 through 9): a match is emitted only when the next position does
   not start a longer one; otherwise the current byte becomes a literal. */
protected void tokenize_lazy(deflater* this, const level_config* config, const u8[] buffer,
                             u32[] head, u32[] prev, u32[] tokens, u32 hash_head, usize* lookahead) {
    usize strstart = this->strstart;
    usize prev_length = this->prev_length;
    usize prev_match = this->prev_match;
    usize match_length = prev_length;
    usize match_start = prev_match;
    if ((hash_head != NIL) && (prev_length < config->lazy)) {
        usize found_start = 0usize;
        usize found = longest_match(buffer, prev, strstart, hash_head, *lookahead, this->window_size,
                                    config, prev_length, &found_start);
        bool too_far = (found == MIN_MATCH) && (strstart - found_start > 4096usize);
        if ((found > prev_length) && (too_far == false)) {
            match_length = found;
            match_start = found_start;
        }
    }
    if ((prev_length >= MIN_MATCH) && (match_length <= prev_length)) {
        push_match(this, tokens, prev_length, (strstart - 1usize) - prev_match);
        usize insert_limit = strstart + *lookahead - MIN_MATCH;
        *lookahead -= prev_length - 1usize;
        for (usize inserted = 2usize; inserted < prev_length; inserted += 1usize) {
            strstart += 1usize;
            if ((strstart <= insert_limit) && (config->chain != 0usize)) {
                insert_string(this, buffer, head, prev, strstart);
            }
        }
        this->match_available = false;
        this->prev_length = MIN_MATCH - 1usize;
        this->prev_match = 0usize;
        this->strstart = strstart + 1usize;
        return;
    }
    if (this->match_available == true) {
        u8 held = buffer[strstart - 1usize];
        push_literal(this, tokens, held);
    }
    this->match_available = true;
    this->prev_length = match_length;
    this->prev_match = match_start;
    this->strstart = strstart + 1usize;
    *lookahead -= 1usize;
}

protected void tokenize_one(deflater* this, const level_config* config, const u8[] buffer, u32[] head,
                            u32[] prev, u32[] tokens, usize* lookahead) {
    u32 hash_head = NIL;
    if ((*lookahead >= MIN_MATCH) && (config->chain != 0usize)) {
        usize hash = hash_bytes(buffer, this->strstart, this->hash_mask);
        hash_head = head[hash];
        prev[this->strstart] = hash_head;
        head[hash] = this->strstart as u32;
    }
    if (config->lazy_matching == true) {
        tokenize_lazy(this, config, buffer, head, prev, tokens, hash_head, lookahead);
    } else {
        tokenize_greedy(this, config, buffer, head, prev, tokens, hash_head, lookahead);
    }
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
    for (usize index = 0usize; index < count; index += 1usize) {
        u8 value = input[pos + index];
        buffer[this->fill + index] = value;
        if (this->form == format::rfc1950) {
            this->check_a = (this->check_a + (value as u32)) % ADLER_MODULUS;
            this->check_b = (this->check_b + this->check_a) % ADLER_MODULUS;
        }
        if (this->form == format::rfc1952) { this->check_a = crc32_update(this->check_a, value); }
    }
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
    array<u32> head_owner = core::replace(&this->head, std.array::create::<u32>());
    array<u32> prev_owner = core::replace(&this->prev, std.array::create::<u32>());
    array<u32> tokens_owner = core::replace(&this->tokens, std.array::create::<u32>());
    array<u8> pending_owner = core::replace(&this->pending, std.array::create::<u8>());
    u8[] buffer = std.array::as_slice_mut(&buffer_owner);
    u32[] head = std.array::as_slice_mut(&head_owner);
    u32[] prev = std.array::as_slice_mut(&prev_owner);
    u32[] tokens = std.array::as_slice_mut(&tokens_owner);
    u8[] pending = std.array::as_slice_mut(&pending_owner);
    while (stop == false) {
        /* 1. Drain staged output before anything else is produced. */
        while ((this->pending_pos < writer.length) && (out_pos < output_size)) {
            output[out_pos] = pending[this->pending_pos];
            out_pos += 1usize;
            this->pending_pos += 1usize;
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
        bool block_emitted = false;
        usize lookahead = this->fill - this->strstart;
        while ((lookahead > 0usize) && (block_emitted == false) &&
               ((lookahead >= MIN_LOOKAHEAD) || (buffer_full == true) || (draining == true))) {
            tokenize_one(this, settings, buffer, head, prev, tokens, &lookahead);
            if ((this->token_count >= window_size) || (tokenized_end(this) - this->block_start >= window_size)) {
                usize block_end = tokenized_end(this);
                flush_block(this, &writer, pending, tokens, buffer, block_end, false);
                block_emitted = true;
            }
        }
        if (block_emitted == true) { continue; }
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
            push_literal(this, tokens, last_literal);
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

/* R-SLIB-DEFLATE-0005: one-shot compression of one complete input. */
array<u8> deflate(const u8[] input, format form, u8 level) throws error, std.alloc::alloc_error {
    deflater engine = deflater::create(form, level, WINDOW_MAX);
    array<u8> result = std.array::create::<u8>();
    array<u8> scratch = std.alloc::bytes(ONE_SHOT_CHUNK, 0u8);
    usize consumed = 0usize;
    usize input_size = len(input);
    while (true) {
        u8[] out_view = std.array::as_slice_mut(&scratch);
        const u8[] rest = input[consumed..input_size];
        progress step = engine.deflate(rest, out_view, flush::finish);
        consumed += step.consumed;
        append_bytes(&result, out_view, step.produced);
        if (step.state == state::end) { return move result; }
    }
}
