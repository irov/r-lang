module test.semantic.switch_dispatch_value;

/* R-STMT-0024: the value converts to the type of the switch operand like an assignment. */
u32 pick(u32 value, u64 wide) {
    step: switch (value) {
    case 0u32:
        continue step (wide);
    default:
        return value;
    }
}

i32 main() {
    return pick(1u32, 2u64) as i32;
}
