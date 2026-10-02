module audit.switch_nested_break_false_rejection;

i32 choose(bool leave) {
    switch (0) {
        case 0:
            if (leave == true) {
                break;
            } else {
                return 0;
            }
            break;
        default:
            return 1;
    }
    return 0;
}

i32 main() {
    i32 first = choose(true);
    i32 second = choose(false);
    return first + second;
}
