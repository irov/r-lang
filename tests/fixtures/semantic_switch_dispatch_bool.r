module test.semantic.switch_dispatch_bool;

/* R-STMT-0024: a labeled switch selects by an integer, char or fieldless enum value, as a switch
   does (R-STMT-0006). */
u32 pick(bool value) {
    step: switch (value) {
    case true:
        continue step (false);
    default:
        return 1u32;
    }
}

i32 main() {
    return pick(true) as i32;
}
