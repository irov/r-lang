module test.semantic.checked_duplicate_throws;

error operation_error {
    i32 code;
};

protected void invalid() throws operation_error, operation_error {
}
