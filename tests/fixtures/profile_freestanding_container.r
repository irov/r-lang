module profile.freestanding_container;

i32 main() {
    array<i32> values = std.array::create::<i32>();
    drop values;
    return 0;
}
