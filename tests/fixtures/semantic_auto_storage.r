module test.semantic.auto_storage;

/* R-NAME-0011: auto requires automatic storage. */
i32 main() {
    static auto counter = 0;
    return counter;
}
