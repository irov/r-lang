module audit.for_statement;

i32 main() {
    i32 sum = 0;
    for (i32 index = 0; index < 4; index += 1) {
        sum += index;
    }
    return sum - 6;
}
