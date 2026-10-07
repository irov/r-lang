module test.semantic.switch_dispatch_outside;

/* R-STMT-0024: a plain continue passes over a labeled switch to the loop around it. */
u32 pick(u32 value) {
    step: switch (value) {
    case 0u32:
        continue;
    default:
        return value;
    }
    return 0u32;
}

i32 main() {
    return pick(1u32) as i32;
}
