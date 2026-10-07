module test.semantic.switch_dispatch_moved;

/* R-STMT-0024: a clause selected anew sees the object states of the clause that selected it, as
   the next iteration of a loop does. */
usize consume(std.string::string text) {
    str view = text;
    return len(view);
}

usize pick(u32 start) throws std.alloc::alloc_error {
    std.string::string word = std.string::from_str("word");
    usize total = 0usize;
    step: switch (start) {
    case 0u32:
        total += consume(move word);
        continue step (1u32);
    case 1u32:
        str view = word;
        total += len(view);
    default:
        break;
    }
    return total;
}

i32 main() {
    return 0;
}
