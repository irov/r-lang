module test.semantic.self_outside_trait;

/* R-TYPE-0041: Self names a type only inside a trait or an implementation. */
i32 stray(const Self* value) {
    return 0;
}

i32 main() {
    return 0;
}
