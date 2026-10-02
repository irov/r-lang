module test.codegen.async_reserve_zero;

/* R-LIB-0016 (L30): a rendezvous channel has no slot to reserve, so reserve on a channel of
   capacity zero is a contract violation. */

async i32 main() {
    std.sync::sync_channel<i32> factory = std.sync::sync_channel::<i32>(0usize);
    std.sync::sync_sender<i32> tx = std.sync::sync_sender(&factory);
    std.sync::receiver<i32> rx = std.sync::sync_receiver(move factory);
    std.sync::reserve_result<i32> slot = await tx.reserve();
    i32 status = 2;
    switch (move slot) {
    case variant std.sync::reserve_result::reserved(move permit): (move permit).send(1);
    case variant std.sync::reserve_result::disconnected: status = 1;
    }
    (move rx) as void;
    return status;
}
