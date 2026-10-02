module test.codegen.sync_channel_factories;

i32 main() {
    try {
        std.sync::channel<i32> factory = std.sync::channel::<i32>();
        std.sync::sender<i32> sender = std.sync::sender(&factory);
        std.sync::receiver<i32> receiver = std.sync::receiver(move factory);
        drop sender;
        drop receiver;

        std.sync::sync_channel<i32> bounded = std.sync::sync_channel::<i32>(4usize);
        std.sync::sync_sender<i32> bounded_sender = std.sync::sync_sender(&bounded);
        drop bounded_sender;
        drop bounded;
        return 0;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 1;
    }
}
