module test.codegen.async_associated_constants;

/* R-TYPE-0050: associated constants in asynchronous generic functions. */

trait Encoded {
    const usize SIZE;
    const bool PACKED = false;
};

struct Point { u32 x; u32 y; };
struct Flag { u8 value; };

impl Encoded for Point {
    const usize SIZE = sizeof(Self);
};

impl Encoded for Flag {
    const usize SIZE = 1usize;
    const bool PACKED = true;
};

@generic<T: Encoded & copy & send & unborrowed>
async usize encoded_bytes(T value) {
    u8[T::SIZE] buffer = {};
    value as void;
    @if (T::PACKED == true) {
        return len(buffer);
    } @else {
        return len(buffer) * 2usize;
    }
}

async i32 main() {
    try {
        task<usize> point = encoded_bytes(Point {.x = 1u32, .y = 2u32});
        usize point_bytes = await move point;
        bool point_ok = point_bytes == 16usize;
        task<usize> flag = encoded_bytes(Flag {.value = 3u8});
        usize flag_bytes = await move flag;
        bool flag_ok = flag_bytes == 1usize && Point::SIZE == 8usize;
        if (point_ok == false || flag_ok == false) {
            return 1;
        }
        return 0;
    } catch (std.async::start_error failure) {
        failure as void;
        return 2;
    }
}
