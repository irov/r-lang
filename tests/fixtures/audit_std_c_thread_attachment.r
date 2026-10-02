module audit.std_c_thread_attachment;

std.c::thread_attachment attach_owned() throws std.c::runtime_error {
    unsafe {
        return std.c::attach_thread();
    }
}

void detach_owned(std.c::thread_attachment attachment) {
    std.c::detach_thread(move attachment);
}

async void attach_and_detach_async() throws std.c::runtime_error {
    unsafe {
        std.c::thread_attachment attachment = std.c::attach_thread();
        std.c::detach_thread(move attachment);
    }
}

i32 main() {
    return 0;
}
