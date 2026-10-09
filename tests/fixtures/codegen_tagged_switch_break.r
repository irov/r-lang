module test.codegen.tagged_switch_break;

/* R-STMT-0004: a break nested in a clause of a switch over variants leaves that switch, not the
   loop around it, after dropping the locals of the clause and running the finally blocks it
   leaves (B5-8: the MIR of a variant switch kept the break target of the loop). */

protected i32 nested_breaks(i32 rounds) throws std.alloc::alloc_error {
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
            str text = word;
            count += 10;
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

protected i32 finally_trace(bool leave) {
    i32 trace = 0;
    i32 round = 0;
    while (round < 2) {
        round += 1;
        o<i32> value = o::some(round);
        switch (move value) {
        case variant o::some(move x):
            try {
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

i32 main() {
    try {
        if (nested_breaks(5) != 45) { return 1; }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 4;
    }
    if (finally_trace(true) != 200) { return 2; }
    if (finally_trace(false) != 2203) { return 3; }
    return 0;
}
