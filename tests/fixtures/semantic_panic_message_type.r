module test.semantic.panic_message_type;

/* R-ERR-0004: the panic message shall have type str. */
i32 main() {
    panic(1);
}
