module audit.postfix_increment;

struct Counter {
    i32 value;
};

i32 main() {
    i32 value = 1;
    value++;
    value--;
    if (value != 1) { return 1; }

    Counter counter = Counter {.value = 4};
    counter.value++;
    counter.value--;
    i32[2] values = {2, 7};
    values[0]++;
    values[1]--;
    if ((values[0] != 3) || (values[1] != 6)) { return 2; }

    for (i32 index = 0; index < 3; index++) {
        counter.value++;
    }
    i32 selected = counter.value == 7 ? 0 : 3;
    return selected;
}
