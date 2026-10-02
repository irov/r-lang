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
