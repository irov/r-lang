module test.codegen.machine_fence_order_panic;

/* Library R-LIB-0028, Core R-MEM-0007 (B11): a fence whose order arrives at run time as relaxed
   ends the process with the contract_violation panic before any fence. */

void fence_with(core::memory_order order) {
    core::atomic_fence(order);
}

i32 main(const str[] args) {
    core::memory_order order = core::memory_order::acquire;
    if (len(args) == 1usize) {
        order = core::memory_order::relaxed;
    }
    fence_with(order);
    return 0;
}
