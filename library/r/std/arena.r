module std.arena;

/* R-SLIB-ARENA-0001: an arena of bytes for parsers. store copies bytes into blocks of a fixed
   size, allocated only when the current block is full, and returns a piece that names them;
   bytes and text read a piece back as a view of the arena. A parser stores the parts of its
   messages first and reads them afterwards, so that one allocation serves many parts and every
   part is freed with the arena. */

/* R-SLIB-ARENA-0001: why the arena refused bytes. */
enum error_code { limit_reached, invalid_piece };

error arena_error { error_code code; };

protected arena_error refusal(error_code code) {
    return arena_error {.code = code};
}

/* R-SLIB-ARENA-0002: the place of stored bytes in an arena. */
struct piece { usize block; usize start; usize length; };

/* R-SLIB-ARENA-0002: the arena; limit bounds the bytes of all its blocks. Each block is made
   with the capacity of its size and never grows past it, so stored bytes never move. */
struct arena {
    protected array<array<u8>> blocks;
    protected usize block_size;
    protected usize limit;
    protected usize last_size;
    protected usize reserved_bytes;
    protected usize used_bytes;
};

arena arena::create(usize block_size, usize limit) {
    usize size = block_size;
    if (size == 0usize) { size = 1usize; }
    array<array<u8>> none = [];
    return arena {.blocks = move none, .block_size = size, .limit = limit, .last_size = 0usize,
                  .reserved_bytes = 0usize, .used_bytes = 0usize};
}

protected void push_block(array<array<u8>>* blocks, array<u8> block) throws std.alloc::alloc_error {
    try {
        blocks->push(move block);
    } catch (std.array::push_error<array<u8>> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Whether the last block, of last_size bytes, has room for length more bytes. */
protected bool fits(const array<array<u8>>* blocks, usize last_size, usize length) {
    usize count = len(*blocks);
    if (count == 0usize) { return false; }
    return last_size - len((*blocks)[count - 1usize]) >= length;
}

/* Appends data to a block whose capacity already holds it: no push reallocates. */
protected void append_bytes(array<u8>* block, const u8[] data) throws std.alloc::alloc_error {
    for (usize index = 0usize; index < len(data); index += 1usize) {
        try {
            block->push(data[index]);
        } catch (std.array::push_error<u8> rejected) {
            switch (move rejected) {
            case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
            }
        }
    }
}

/* R-SLIB-ARENA-0003: copies data into the arena; a new block holds the larger of block_size and
   the data. A block that would take the arena past its limit is refused with limit_reached. */
piece arena::store(arena* this, const u8[] data) throws arena_error, std.alloc::alloc_error {
    if (fits(&this->blocks, this->last_size, len(data)) == false) {
        usize size = this->block_size;
        if (len(data) > size) { size = len(data); }
        if (size > this->limit || this->reserved_bytes > this->limit - size) {
            throw refusal(error_code::limit_reached);
        }
        array<u8> block = std.array::with_capacity::<u8>(size);
        push_block(&this->blocks, move block);
        this->reserved_bytes += size;
        this->last_size = size;
    }
    usize last = len(this->blocks) - 1usize;
    usize start = len(this->blocks[last]);
    append_bytes(&this->blocks[last], data);
    this->used_bytes += len(data);
    return piece {.block = last, .start = start, .length = len(data)};
}

piece arena::store_text(arena* this, str text) throws arena_error, std.alloc::alloc_error {
    const u8[] raw_bytes = text;
    return this->store(raw_bytes);
}

/* R-SLIB-ARENA-0004: the bytes of a piece, as a view of the arena. */
const u8[] arena::bytes(const arena* this, piece where) throws arena_error {
    if (where.block >= len(this->blocks)) { throw refusal(error_code::invalid_piece); }
    const u8[] block = std.array::as_slice(&this->blocks[where.block]);
    if (where.start > len(block) || where.length > len(block) - where.start) {
        throw refusal(error_code::invalid_piece);
    }
    return block[where.start..where.start + where.length];
}

/* R-SLIB-ARENA-0004: the text of a piece stored from text; bytes that are not UTF-8 are
   invalid_piece. */
str arena::text(const arena* this, piece where) throws arena_error {
    const u8[] raw_bytes = this->bytes(where);
    try {
        return core::validate_utf8(raw_bytes);
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    throw refusal(error_code::invalid_piece);
}

/* R-SLIB-ARENA-0005: the stored bytes, and the bytes of the blocks that hold them. */
usize arena::used(const arena* this) {
    return this->used_bytes;
}

usize arena::reserved(const arena* this) {
    return this->reserved_bytes;
}

/* R-SLIB-ARENA-0005: frees every block; earlier pieces no longer name stored bytes. */
void arena::reset(arena* this) {
    array<array<u8>> none = [];
    array<array<u8>> old = core::replace(&this->blocks, move none);
    drop old;
    this->last_size = 0usize;
    this->reserved_bytes = 0usize;
    this->used_bytes = 0usize;
}
