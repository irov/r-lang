module test.regression.async_main_throws;

error Failure {
    failed,
};

async i32 main() throws Failure {
    throw Failure::failed;
}
