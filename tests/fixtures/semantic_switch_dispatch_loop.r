module test.semantic.switch_dispatch_loop;

/* R-STMT-0024: continue with a value names a labeled switch, not a loop. */
u32 pick(u32 value) {
    u32 total = 0u32;
    outer: while (total < value) {
        total += 1u32;
        continue outer (total);
    }
    return total;
}

i32 main() {
    return pick(1u32) as i32;
}
