module regression.exclusive_reborrow;

void write_value(i32* value) {
    *value = 29;
}

i32 main() {
    i32 value = 17;
    i32* parent = &value;
    write_value(&*parent);
    return *parent - 29;
}
