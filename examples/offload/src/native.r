module example.offload.native;

@link(name = "system.libc", kind = "system")
@header("unistd.h")
extern "C" {
    @safety("OFFLOAD-USLEEP", "usleep only suspends the calling thread for the given time")
    c_int usleep(c_uint microseconds);
}

/* A blocking C call: usleep suspends the thread that calls it. Started with std.async::blocking
   it occupies a thread of the blocking call pool and never an executor worker. */
u32 nap(u32 milliseconds) {
    unsafe {
        c_int status = usleep((milliseconds * 1000u32) as c_uint);
        status as void;
    }
    return milliseconds;
}

/* A plan of naps by index. An index past its end panics with bounds on the pool thread that runs
   the call (Core R-ERR-0009): the task of the call ends with that panic. */
u32 nap_at(u32 index) {
    u32[3] plan = {10u32, 20u32, 30u32};
    return nap(plan[index as usize]);
}
