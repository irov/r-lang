module test.regression.link_plan_table_operations;

// M24-1: a synchronous program that calls operations of the std.async and std.test tables
// names their libraries in its link plan.

i32 main() {
    std.test::fail_allocation_at(0u64);
    u64 attempts = std.test::allocation_attempts();
    u64 running = std.async::task_id();
    i32 status = 0;
    if (attempts != 0u64) { status += 1; }
    if (running != 0u64) { status += 2; }
    return status;
}
