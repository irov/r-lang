module test.codegen.async_broadcast_zero;

/* R-SLIB-ASYNC-0016 (L30): every subscriber queue holds at least one value, so a broadcast of
   capacity zero is a contract violation. */

async i32 main() {
    std.async::broadcast<i32> hub = std.async::broadcast(0usize);
    return hub.send(1) as i32;
}
