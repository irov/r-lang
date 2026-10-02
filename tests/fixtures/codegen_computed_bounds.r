module test.codegen.computed_bounds;

/* R-TYPE-0047: bounds and constant arguments computed by calls over generic parameters. */

@generic<T>
usize size_of() {
    return sizeof(T);
}

usize round_up(usize value, usize unit) {
    return ((value + unit - 1usize) / unit) * unit;
}

@generic<T>
struct Buf { u8[size_of::<T>()] bytes; };

@generic<const usize N>
struct Words { u32[round_up(N, 4usize)] words; };

@generic<const usize N>
struct Fixed { u8[N] data; };

/* A constant argument computed from the declaration's parameters. */
@generic<T>
struct Holder { Fixed<size_of::<T>() * 2usize> inner; };

/* Instances closed at module scope, before function bodies are checked. */
struct Frame { Buf<u64> payload; Words<3usize> words; };

usize payload_bytes(Buf<u32> buffer) {
    return len(buffer.bytes);
}

/* The signature and the local spell one formula, so they are one type. */
@generic<T: copy>
u8[round_up(size_of::<T>(), 8usize)] padded(T value) {
    value as void;
    u8[round_up(size_of::<T>(), 8usize)] result = {};
    return result;
}

@generic<T, const usize N>
struct Table { u32[round_up(size_of::<T>() * N, 16usize)] words; };

i32 main() {
    Frame frame = {};
    Buf<u32> four = {};
    Holder<u16> holder = {};
    Table<u16, 3usize> table = {};
    u8[8] padded_byte = padded(1u8);
    bool padded_ok = padded_byte[7] == 0u8;
    bool frame_ok = len(frame.payload.bytes) == 8usize && len(frame.words.words) == 4usize;
    bool local_ok = payload_bytes(four) == 4usize && len(holder.inner.data) == 4usize &&
                    len(table.words) == 16usize;
    if (frame_ok == false || local_ok == false || padded_ok == false) {
        return 1;
    }
    return 0;
}
