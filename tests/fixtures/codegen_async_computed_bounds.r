module test.codegen.async_computed_bounds;

/* R-TYPE-0047: computed bounds in asynchronous generic functions, lowered through MIR. */

@generic<T>
usize size_of() {
    return sizeof(T);
}

@generic<T>
struct Buf { u8[size_of::<T>()] bytes; };

struct Frame { Buf<u64> payload; };

@generic<T: copy & send & unborrowed>
async usize measure(T value) {
    value as void;
    u8[size_of::<T>() * 2usize] scratch = {};
    Buf<T> buffer = {};
    return len(scratch) + len(buffer.bytes);
}

async i32 main() {
    Frame frame = {};
    bool frame_ok = len(frame.payload.bytes) == 8usize;
    try {
        task<usize> pending = measure(1u32);
        usize total = await move pending;
        bool total_ok = total == 12usize;
        if (frame_ok == false || total_ok == false) {
            return 1;
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 2;
    }
}
