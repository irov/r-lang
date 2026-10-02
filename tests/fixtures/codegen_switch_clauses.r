module test.codegen.switch_clauses;

i32 main() {
    i32 index = 0;
    i32 sum = 0;
    while (index < 4) {
        switch (index) {
            case 0:
                sum += 1;
                fallthrough;
            case 1:
                sum += 2;
                break;
            case 2:
                index += 1;
                continue;
            default:
                sum += 4;
                break;
        }
        index += 1;
    }
    if (sum == 9) {
        return 0;
    } else {
        return 1;
    }
}
