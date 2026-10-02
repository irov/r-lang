module test.codegen.dead_new_helpers;

// M30-1: functions that no root calls still pass through the first pass of the C17 emitter; the
// helpers of their new own, new arc and new rc shall not be emitted, or the generated C would
// define unused static functions.
struct Box { i32 value; };

protected own Box* unused_own(i32 value) {
    return new Box {.value = value};
}

protected arc Box unused_arc(i32 value) {
    return new arc Box {.value = value};
}

protected rc Box unused_rc(i32 value) {
    return new rc Box {.value = value};
}

i32 main() {
    return 0;
}
