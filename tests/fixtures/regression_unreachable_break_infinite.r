module regression.unreachable_break_infinite;

i32 never_returns() {
    while (true) {
        if (false) {
            break;
        }
    }
}

i32 main() {
    return 0;
}
