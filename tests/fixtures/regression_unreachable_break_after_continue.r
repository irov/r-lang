module regression.unreachable_break_after_continue;

i32 never_returns() {
    while (true) {
        continue;
        break;
    }
}

i32 main() {
    return 0;
}
