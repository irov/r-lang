module audit.prefix_increment;

i32 main() {
    i32 value = 1;
    ++value;
    --value;
    if (value != 1) { return 1; }

    i32[2] values = {3, 5};
    ++values[1];
    --values[0];
    if ((values[0] != 2) || (values[1] != 6)) { return 2; }

    u8 wrapped = 0;
    --wrapped;
    i32 selected = wrapped == 255 ? 0 : 3;
    return selected;
}
