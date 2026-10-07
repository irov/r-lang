module test.semantic.switch_dispatch_plain;

/* R-STMT-0024: continue of a labeled switch gives the value to select by. */
u32 pick(u32 value) {
    step: switch (value) {
    case 0u32:
        continue step;
    default:
        return value;
    }
}

i32 main() {
    return pick(1u32) as i32;
}
