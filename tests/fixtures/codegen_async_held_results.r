module test.codegen.async_held_results;

/* R-BORROW-0009 (L15.1): an async frame keeps results taken from what a decoder holds while it
   borrows the decoder again. Views end before each await (R-BORROW-0024), so the frame decodes
   after its awaits. */
struct Decoder {
    const u8[] input;
    usize at;
};

const u8[] take(Decoder* this, usize n) {
    const u8[] part = this->input[this->at..this->at + n];
    this->at += n;
    return part;
}

const u8[] Decoder::take_one(Decoder* this) {
    return take(this, 1usize);
}

u32 sum(const u8[] bytes) {
    u32 total = 0u32;
    for (usize i = 0usize; i < len(bytes); i += 1usize) { total += bytes[i] as u32; }
    return total;
}

protected async i32 tick(i32 value) {
    return value + 1;
}

protected async i32 work() throws std.async::start_error {
    i32 one = await tick(0);
    i32 two = await tick(one);
    if (two != 2) { return 6; }
    u8[6] data = {1u8, 2u8, 3u8, 4u8, 5u8, 6u8};
    const u8[] view = &data;
    Decoder d = Decoder {.input = view, .at = 0usize};
    const u8[] head = take(&d, 1usize);
    const u8[] body = take(&d, 2usize);
    u32 pair = (sum(head) * 10u32) + sum(body);
    if (pair != 15u32) { return 1; }
    Decoder g = Decoder {.input = view, .at = 0usize};
    const u8[] a = take(&g, 1usize);
    Decoder h = move g;
    const u8[] b = take(&h, 1usize);
    if ((sum(a) + sum(b)) != 3u32) { return 2; }
    Decoder m = Decoder {.input = view, .at = 0usize};
    Decoder* cursor = &m;
    const u8[] last = view[0usize..0usize];
    u32 seen = 0u32;
    for (usize i = 0usize; i < 3usize; i += 1usize) {
        seen += sum(last);
        last = cursor->take_one();
    }
    u32 tail = (seen * 10u32) + sum(last);
    if (tail != 33u32) { return 3; }
    return 0;
}

async i32 main() {
    try {
        return await work();
    } catch (std.async::start_error failure) {
        failure as void;
        return 90;
    }
}
