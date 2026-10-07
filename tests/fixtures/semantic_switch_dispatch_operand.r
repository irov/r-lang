module test.semantic.switch_dispatch_operand;

/* R-STMT-0024: a labeled switch selects by an integer, char, bool or fieldless enum value. */
u32 pick(o<u32> value) {
    step: switch (value) {
    case variant o::some(inner):
        return *inner;
    case variant o::none:
        return 0u32;
    }
}

i32 main() {
    return pick(o::some(1u32)) as i32;
}
