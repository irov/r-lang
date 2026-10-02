module example.ingest.parse;
import std.arena;

/* One field of a message: the pieces of its key and value in the arena of the batch. */
struct field { std.arena::piece key; std.arena::piece value; };

/* A message: its fields in order. Only the pieces live here; the bytes stay in the arena. */
struct message { array<field> fields; };

/* Why a line is not a message: a part without '=', a part with an empty key, or a line larger
   than the arena admits. */
@derive(format)
enum problem { missing_equals, empty_key, too_large };

error parse_error { problem reason; usize position; };

protected void add(array<field>* fields, field item) throws std.alloc::alloc_error {
    try {
        fields->push(item);
    } catch (std.array::push_error<field> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* The index of the first '=' of part, or its length when it has none. */
protected usize equals_in(const u8[] part) {
    for (usize at = 0usize; at < len(part); at += 1usize) {
        if (part[at] == 61u8) { return at; }
    }
    return len(part);
}

/* Stores one "key=value" part that starts at position of its line. */
protected field store_part(std.arena::arena* place, const u8[] part, usize position)
    throws parse_error, std.arena::arena_error, std.alloc::alloc_error {
    usize equals = equals_in(part);
    throw (equals == len(part)) parse_error {.reason = problem::missing_equals, .position = position};
    throw (equals == 0usize) parse_error {.reason = problem::empty_key, .position = position};
    try {
        std.arena::piece key = place->store(part[0usize..equals]);
        std.arena::piece value = place->store(part[equals + 1usize..len(part)]);
        return field {.key = key, .value = value};
    } catch (std.arena::arena_error refused) {
        /* A block past the limit of the arena makes the line too large; store refuses with
           nothing else. */
        throw (refused.code != std.arena::error_code::limit_reached) refused;
    }
    throw parse_error {.reason = problem::too_large, .position = position};
}

/* Parses "key=value;key=value" into the arena: the bytes of every key and value are copied
   there once, and the message keeps only their pieces. Empty parts are skipped. */
message parse(std.arena::arena* place, str line)
    throws parse_error, std.arena::arena_error, std.alloc::alloc_error {
    const u8[] raw_bytes = line;
    array<field> fields = [];
    usize start = 0usize;
    for (usize index = 0usize; index <= len(raw_bytes); index += 1usize) {
        if (index == len(raw_bytes) || raw_bytes[index] == 59u8) {
            if (index > start) {
                add(&fields, store_part(place, raw_bytes[start..index], start));
            }
            start = index + 1usize;
        }
    }
    return message {.fields = move fields};
}
