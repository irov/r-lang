module test.regression.sync_main_throws;

error Failure {
    failed,
};

i32 main() throws Failure {
    throw Failure::failed;
}
