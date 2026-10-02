module test.codegen.loop_jumps;

i32 main() {
    i32 value = 0;
    i32 sum = 0;
    while (value < 10) {
        value += 1;
        if (value == 3) {
            continue;
        }
        if (value == 7) {
            break;
        }
        sum += value;
    }
    if (sum == 18) {
        return 0;
    } else {
        return 1;
    }
}
