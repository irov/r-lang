module example.workers.queue;

struct CommandStorage1 { std.string::string value; };
struct CommandStorage2 { std.string::string value; };

struct Record { usize index; usize bytes; u32 checksum; };
error WorkerError { QueueFailed, Panicked };

Record inspect(const std.string::string* message, usize index) {
    const u8[] data = *message;
    usize length = len(data);
    u32 checksum = std.hash::crc32(data);
    return Record { .index = index, .bytes = length, .checksum = checksum };
}

bool produce(array<std.string::string> messages, std.sync::sender<Record> output) {
    usize index = 0usize;
    for (const std.string::string* message in &messages) {
        Record record = inspect(message, index);
        std.sync::send_result<Record> result = output.send(record);
        switch (move result) {
        case variant std.sync::send_result::sent: break;
        case variant std.sync::send_result::disconnected(move returned): return false;
        case variant std.sync::send_result::allocation_failed(move returned): return false;
        }
        index += 1usize;
        std.thread::yield_now();
    }
    return true;
}

void produce_bounded(array<std.string::string> messages, std.sync::sync_sender<Record> output) {
    usize index = 0usize;
    for (const std.string::string* message in &messages) {
        Record record = inspect(message, index);
        std.sync::try_send_result<Record> immediate = output.try_send(record);
        switch (move immediate) {
        case variant std.sync::try_send_result::sent: break;
        case variant std.sync::try_send_result::full(move returned):
            std.sync::send_result<Record> waited = output.send(returned);
            switch (move waited) {
            case variant std.sync::send_result::sent: break;
            case variant std.sync::send_result::disconnected(move rejected): return;
            case variant std.sync::send_result::allocation_failed(move rejected): return;
            }
            break;
        case variant std.sync::try_send_result::disconnected(move returned): return;
        }
        index += 1usize;
    }
}

void append(std.string::string* output, Record record) throws std.alloc::alloc_error {
    std.string::string line = f"{record.index}: bytes={record.bytes} crc32={record.checksum}\n";
    str text = line;
    output->append(text);
}

std.string::string collect(std.sync::receiver<Record> input, usize expected)
    throws std.alloc::alloc_error, WorkerError {
    std.string::string output = std.string::create();
    usize received = 0usize;
    bool completed = false;
    while (completed == false) {
        // Avoid blocking when a record is already queued; block only on an empty live channel.
        std.sync::try_recv_result<Record> available = input.try_recv();
        switch (move available) {
        case variant std.sync::try_recv_result::received(move record):
            append(&output, record); received += 1usize; break;
        case variant std.sync::try_recv_result::disconnected: completed = true; break;
        case variant std.sync::try_recv_result::empty:
            std.sync::recv_result<Record> waited = input.recv();
            switch (move waited) {
            case variant std.sync::recv_result::received(move record):
                append(&output, record); received += 1usize; break;
            case variant std.sync::recv_result::disconnected: completed = true; break;
            }
            break;
        }
    }
    throw (received != expected) WorkerError::QueueFailed;
    return move output;
}

std.string::string panic_description(const std.thread::panic_report* report) throws std.alloc::alloc_error {
    constexpr str category = report->category();
    str text = report->text();
    return f"worker panic: {category}: {text}\n";
}

std.string::string unbounded(array<std.string::string> messages, bool detached)
    throws std.alloc::alloc_error, std.thread::thread_error, WorkerError {
    usize expected = len(messages);
    std.sync::channel<Record> factory = std.sync::channel::<Record>();
    std.sync::sender<Record> initial = factory.sender();
    std.sync::sender<Record> output = initial.clone();
    std.sync::receiver<Record> input = (move factory).receiver();
    std.thread::join_handle<bool> worker = std.thread::spawn(produce, move messages, move output);
    drop initial;
    CommandStorage2 state_report = {.value = std.string::create()};
    if (detached == true) {
        (move worker).detach();
        state_report.value = collect(move input, expected);
    } else {
        state_report.value = collect(move input, expected);
        std.thread::join_result<bool> completion = (move worker).join();
        switch (move completion) {
        case variant std.thread::join_result::returned(move delivered):
            throw (delivered == false) WorkerError::QueueFailed; break;
        case variant std.thread::join_result::panicked(move failure):
            state_report.value = panic_description(&failure); break;
        }
    }
    return core::replace(&state_report.value, std.string::create());
}

std.string::string bounded(array<std.string::string> messages, usize capacity)
    throws std.alloc::alloc_error, std.thread::thread_error, WorkerError {
    usize expected = len(messages);
    std.sync::sync_channel<Record> factory = std.sync::sync_channel::<Record>(capacity);
    std.sync::sync_sender<Record> initial = factory.sender();
    std.sync::sync_sender<Record> output = initial.clone();
    std.sync::receiver<Record> input = (move factory).receiver();
    std.thread::join_handle<void> worker = std.thread::spawn(produce_bounded, move messages, move output);
    // Receiver ownership now controls cancellation: dropping it disconnects a blocked sender.
    (move worker).detach();
    drop initial;
    CommandStorage1 state_report = {.value = collect(move input, expected)};
    return core::replace(&state_report.value, std.string::create());
}
