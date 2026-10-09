module test.codegen.async_tagged_switch_break;

/* R-STMT-0004 in async bodies: a break nested in a clause of a switch over variants leaves that
   switch, also across an await, after dropping the locals of the clause and running the finally
   blocks it leaves (B5-8). */

async i32 later(i32 value) throws std.error::fault {
    await std.time::sleep_for(std.time::duration_from_parts(0i64, 1000000u32));
    return value;
}

protected async i32 nested_breaks(i32 rounds) throws std.error::fault {
    i32 count = 0;
    i32 index = 0;
    while (index < rounds) {
        index += 1;
        o<i32> value = o::some(index);
        switch (move value) {
        case variant o::some(move x):
            count += 1;
            if (x == 2) {
                break;
            }
            std.string::string word = std.string::from_str("ten");
            i32 step = await later(10);
            str text = word;
            count += step;
            if (len(text) == 3usize) {
                break;
            }
            count += 1000;
        case variant o::none:
            break;
        }
    }
    return count;
}

protected async i32 finally_trace(bool leave) throws std.error::fault {
    i32 trace = 0;
    i32 round = 0;
    while (round < 2) {
        round += 1;
        o<i32> value = o::some(round);
        switch (move value) {
        case variant o::some(move x):
            try {
                trace += await later(x) - x;
                if (leave == true) {
                    break;
                }
                trace += x;
            } finally {
                trace += 100;
            }
            trace += 1000;
        case variant o::none:
            return -1;
        }
    }
    return trace;
}

async i32 main() {
    try {
        if (await nested_breaks(5) != 45) { return 1; }
        if (await finally_trace(true) != 200) { return 2; }
        if (await finally_trace(false) != 2203) { return 3; }
        return 0;
    } catch (std.error::fault failure) {
        failure as void;
        return 4;
    }
}
