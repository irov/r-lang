module test.codegen.async_switch_dispatch;

/* R-STMT-0024 (L46) in an async body: a clause awaits before it selects anew, and the selector
   and the owned objects of a clause live in the frame across the await. */

async u32 later(u32 value) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    return value;
}

async usize steps(u32 start) throws std.error::fault {
    usize total = 0usize;
    walk: switch (start) {
    case 0u32:
        std.string::string word = std.string::from_str("zero");
        u32 next = await later(1u32);
        str text = word;
        total += len(text);
        continue walk (next);
    case 1u32:
        total += 10usize;
        continue walk (await later(2u32));
    case 2u32:
        for (u32 round = 0u32; round < 3u32; round += 1u32) {
            if (round == 1u32) {
                continue walk (3u32);
            }
            total += 100usize;
        }
    default:
        total += 1000usize;
        break walk;
    }
    return total;
}

async i32 main() {
    try {
        usize total = await steps(0u32);
        if (total != 1114usize) { return 1; }
        return 0;
    } catch (std.error::fault failure) {
        failure as void;
    }
    return 9;
}
