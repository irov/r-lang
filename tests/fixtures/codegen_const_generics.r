module test.codegen.const_generics;

@generic<T, const usize N>
struct Buffer { T[N] data; };
@generic<const usize N>
@noalloc @nonblocking
usize capacity(const (u8[N])* data) {
    static const usize amount = N;
    u8[N + 1usize] scratch = {};
    usize count = len(scratch);
    return count - 1usize + amount - N;
}
@generic<T: copy, const usize N>
@noalloc @nonblocking
usize inspect(const Buffer<T, N>* packet) { return N; }
@generic<const usize N>
struct Padded { Buffer<u8, N + 1usize> packet; };
@generic<const usize N>
enum Message { Empty, Data(Buffer<u8, N>), };
@generic<const usize N>
usize message_size(Message<N> message) {
    switch (message) {
    case variant Message<N>::Empty: return 0usize;
    case variant Message<N>::Data(packet): return N;
    }
}
i32 main() {
    try {
        Buffer<u8, 4usize> packet = {.data={1u8, 2u8, 3u8, 4u8}};
        Buffer<u8, 2usize + 2usize> copy = packet;
        usize count = capacity(&packet.data);
        usize inferred = inspect(&copy);
        if (count != 4usize || inferred != 4usize || copy.data[3] != 4u8) { throw TestAssertionFailed {.code = 1}; }
        Padded<3usize> padded = {.packet=copy};
        Message<4usize> message = Message<4usize>::Data(padded.packet);
        usize payload = message_size(message);
        if (payload != 4usize) { throw TestAssertionFailed {.code = 2}; }
        const usize small = 2usize;
        u8[small] shorter = {};
        usize other = capacity(&shorter);
        usize repeated = capacity(&packet.data);
        if (other != 2usize || repeated != 4usize) { throw TestAssertionFailed {.code = 3}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
