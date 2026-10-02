module codegen.async_core_slice_from_raw_parts_in;

/* The address is atomic, so the buffer is Send and may live across an await. */
struct buffer { atomic raw u8*? data; usize size; };

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

struct holder { const u8[] bytes; };

holder wrap(const buffer* source) {
    return holder {.bytes = source->bytes()};
}

const u8[] text_bytes(str text) {
    const u8[] raw_bytes = text;
    unsafe {
        raw const u8* start = &raw_bytes[0] as raw const u8*;
        return core::slice_from_raw_parts_in(text, start, len(raw_bytes));
    }
}

async i32 main() {
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
    u8 before = 0u8;
    {
        holder h = wrap(&b);
        const u8[] t = text_bytes("hey");
        u8 letter = t[1];
        u8 last = h.bytes[3];
        if (letter == 101u8) { before = last; }
    }
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    u8[] writable = b.bytes_mut();
    writable[0] = 7u8;
    const u8[] view = b.bytes();
    u8 first = view[0];
    if (before == 4u8 && first == 7u8) { return 0; }
    return 1;
}
