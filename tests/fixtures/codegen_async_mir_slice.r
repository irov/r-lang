module test.codegen.async_mir_slice;

struct Scratch {
    u8[8] bytes;
};

protected async i32 ready() {
    return 5;
}

async i32 main() {
    own Scratch* scratch = new Scratch{};

    {
        i32 lower = 1;
        i32 upper = 6;
        u8[] middle = scratch->bytes[lower..upper];
        const u8[] readonly = middle;
        const u8[] tail = readonly[1..5];
        i32 signed_index = 3;
        if ((len(middle) != 5) || (len(tail) != 4)) {
            return 1;
        }
        if ((scratch->bytes[1] != 0) || (tail[0] != 0) || (tail[3] != 0)) {
            return 2;
        }
        if (scratch->bytes[signed_index] != 0) {
            return 5;
        }
    }

    try {
        task<i32> operation = ready();
        i32 value = await move operation;
        {
            const u8[] suffix = scratch->bytes[1..6];
            if ((value != 5) || (len(suffix) != 5) || (suffix[0] != 0) ||
                (suffix[4] != 0)) {
                return 3;
            }
        }
        return 0;
    } catch (std.async::start_error error) {
        error as void;
        return 4;
    }
}
