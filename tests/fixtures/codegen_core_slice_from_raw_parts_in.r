module codegen.core_slice_from_raw_parts_in;

/* R-UNSAFE-0008 (L38): views of storage that a raw address designates, anchored to the object
   that keeps that storage alive, returned from methods and kept in a struct. */
struct buffer { atomic raw u8*? data; usize size; };

struct holder { const u8[] bytes; };

protected raw u8*? address_of(const buffer* source) {
    return core::atomic_load(&source->data, core::memory_order::relaxed);
}

const u8[] buffer::bytes(const buffer* this) {
    raw u8*? data = address_of(this);
    unsafe { return core::slice_from_raw_parts_in(this, data as raw const u8*?, this->size); }
}

u8[] buffer::bytes_mut(buffer* this) {
    raw u8*? data = address_of(this);
    unsafe { return core::slice_from_raw_parts_in_mut(this, data, this->size); }
}

buffer make_buffer(array<u8>* storage) {
    unsafe { return buffer {.data = &(*storage)[0usize] as raw u8*, .size = len(*storage)}; }
}

holder wrap(const buffer* source) {
    return holder {.bytes = source->bytes()};
}

/* The same bytes as little-endian words, anchored to the byte view. */
const u16[] as_words(const u8[] bytes) {
    unsafe {
        raw const u8* start = &bytes[0] as raw const u8*;
        return core::slice_from_raw_parts_in(bytes, start as raw const void* as raw const u16*,
                                             len(bytes) / 2usize);
    }
}

const u8[] text_bytes(str text) {
    const u8[] raw_bytes = text;
    unsafe {
        raw const u8* start = &raw_bytes[0] as raw const u8*;
        return core::slice_from_raw_parts_in(text, start, len(raw_bytes));
    }
}

i32 main() {
    array<u8> storage = std.array::with_capacity::<u8>(4usize);
    for (u8 value = 1u8; value <= 4u8; value += 1u8) {
        try {
            storage.push(value);
        } catch (std.array::push_error<u8> rejected) {
            rejected as void;
            return 2;
        }
    }
    buffer b = make_buffer(&storage);
    u8[] writable = b.bytes_mut();
    writable[0] = 9u8;
    holder kept = wrap(&b);
    const u16[] words = as_words(kept.bytes);
    const u8[] letters = text_bytes("anchor");
    u8 first = kept.bytes[0];
    u16 high = words[1];
    usize word_count = len(words);
    u8 letter = letters[1];
    unsafe {
        raw const u8*? nothing = null;
        const u8[] empty = core::slice_from_raw_parts_in(&b, nothing, 0usize);
        const u8[] local = core::slice_from_raw_parts_in(&b, address_of(&b) as raw const u8*?, 2usize);
        usize empty_count = len(empty);
        u8 local_second = local[1];
        if (first == 9u8 && high == 1027u16 && word_count == 2usize && letter == 110u8 &&
            empty_count == 0usize && local_second == 2u8) {
            return 0;
        }
    }
    return 1;
}
