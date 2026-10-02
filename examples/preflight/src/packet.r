module example.preflight.packet;
import example.calculator.common::{Usage};

@generic<T>
struct View { const T[] data; };

@must_use
struct Summary { usize size; u32 checksum; };

@generic<T>
@noalloc @nonblocking
View<T> bounded(const T[] source, usize capacity) throws Usage {
    // The generic closure preserves the input region and checks its body at definition.
    fn @noalloc @nonblocking View<T> select(const T[] data) move(capacity) throws Usage {
        usize size = len(data);
        throw (size > capacity) Usage {.message = "packet exceeds inspection capacity"};
        return View<T> {.data = data};
    }
    View<T> result = select(source);
    return result;
}

@generic<F: fn @noalloc @nonblocking(const u8[]) -> u32>
@must_use @noalloc @nonblocking
Summary summarize(const F* checksum, View<u8> view) {
    usize size = len(view.data);
    u32 digest = checksum(view.data);
    return Summary {.size = size, .checksum = digest};
}

async std.string::string inspect(std.string::string source)
    throws Usage, std.alloc::alloc_error, std.async::start_error {

        const u8[] data = source.as_bytes();
        View<u8> view = bounded(data, 60usize);
        fn @noalloc @nonblocking u32 checksum(const u8[] value) {
            u32 digest = std.hash::crc32(value);
            return digest;
        }
        Summary summary = summarize(&checksum, view);
    // Only owned/Copy state crosses the await. The borrowed view ends above.
    async fn std.string::string publish() move(summary) throws std.alloc::alloc_error {
        return f"bytes={summary.size} crc32={summary.checksum}\n";
    }
    std.string::string report = await publish();
    return move report;
}
