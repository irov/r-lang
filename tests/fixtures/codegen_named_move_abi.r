module codegen.named_move_abi;

struct payload {
    u32 value;
};

std.c::c_string pass_c_string(std.c::c_string value) {
    std.c::c_string local = move value;
    return move local;
}

std.c::handle pass_c_handle(std.c::handle value) {
    std.c::handle local = move value;
    return move local;
}

std.c::thread_attachment pass_c_thread_attachment(std.c::thread_attachment value) {
    std.c::thread_attachment local = move value;
    return move local;
}

std.secret::buffer pass_secret_buffer(std.secret::buffer value) {
    std.secret::buffer local = move value;
    return move local;
}

std.format::builder pass_format_builder(std.format::builder value) {
    std.format::builder local = move value;
    return move local;
}

std.json::value pass_json_value(std.json::value value) {
    std.json::value local = move value;
    return move local;
}
void drop_json_value(std.json::value value) { drop value; }

std.json::number pass_json_number(std.json::number value) {
    std.json::number local = move value;
    return move local;
}
void drop_json_number(std.json::number value) { drop value; }

std.json::error pass_json_error(std.json::error value) {
    std.json::error local = move value;
    return move local;
}
void drop_json_error(std.json::error value) { drop value; }

std.string::string pass_string(std.string::string value) {
    std.string::string local = move value;
    return move local;
}

std.string::from_bytes_result pass_string_from_bytes_result(
    std.string::from_bytes_result value) {
    std.string::from_bytes_result local = move value;
    return move local;
}

std.fs::directory pass_directory(std.fs::directory value) {
    std.fs::directory local = move value;
    return move local;
}

std.fs::directory_entry pass_directory_entry(std.fs::directory_entry value) {
    std.fs::directory_entry local = move value;
    return move local;
}

std.fs::directory_iter pass_directory_iter(std.fs::directory_iter value) {
    std.fs::directory_iter local = move value;
    return move local;
}

std.fs::directory_next_result pass_directory_next_result(
    std.fs::directory_next_result value) {
    std.fs::directory_next_result local = move value;
    return move local;
}

std.fs::file pass_file(std.fs::file value) {
    std.fs::file local = move value;
    return move local;
}

std.fs::path pass_path(std.fs::path value) {
    std.fs::path local = move value;
    return move local;
}

std.io::input pass_input(std.io::input value) {
    std.io::input local = move value;
    return move local;
}

std.io::output pass_output(std.io::output value) {
    std.io::output local = move value;
    return move local;
}

std.io::read_result pass_read_result(std.io::read_result value) {
    std.io::read_result local = move value;
    return move local;
}

std.io::write_result pass_io_write_result(std.io::write_result value) {
    std.io::write_result local = move value;
    return move local;
}

std.io::shared_write_result pass_shared_write_result(std.io::shared_write_result value) {
    std.io::shared_write_result local = move value;
    return move local;
}

std.io::write_all_result pass_write_all_result(std.io::write_all_result value) {
    std.io::write_all_result local = move value;
    return move local;
}

std.fs::write_file_result pass_write_result(std.fs::write_file_result value) {
    std.fs::write_file_result local = move value;
    return move local;
}

std.sync::once pass_once(std.sync::once value) {
    std.sync::once local = move value;
    return move local;
}

std.sync::barrier pass_barrier(std.sync::barrier value) {
    std.sync::barrier local = move value;
    return move local;
}

std.sync::condvar pass_condvar(std.sync::condvar value) {
    std.sync::condvar local = move value;
    return move local;
}

std.thread::panic_report pass_panic_report(std.thread::panic_report value) {
    std.thread::panic_report local = move value;
    return move local;
}

std.thread::thread pass_thread(std.thread::thread value) {
    std.thread::thread local = move value;
    return move local;
}

std.net::tcp_connection pass_tcp_connection(std.net::tcp_connection value) {
    std.net::tcp_connection local = move value;
    return move local;
}

std.net::tcp_listener pass_tcp_listener(std.net::tcp_listener value) {
    std.net::tcp_listener local = move value;
    return move local;
}

std.net::tcp_read_result pass_tcp_read_result(std.net::tcp_read_result value) {
    std.net::tcp_read_result local = move value;
    return move local;
}

std.net::tcp_write_all_result pass_tcp_write_all_result(std.net::tcp_write_all_result value) {
    std.net::tcp_write_all_result local = move value;
    return move local;
}

std.net::tcp_write_result pass_tcp_write_result(std.net::tcp_write_result value) {
    std.net::tcp_write_result local = move value;
    return move local;
}

std.net::udp_receive_result pass_udp_receive_result(std.net::udp_receive_result value) {
    std.net::udp_receive_result local = move value;
    return move local;
}

std.net::udp_send_result pass_udp_send_result(std.net::udp_send_result value) {
    std.net::udp_send_result local = move value;
    return move local;
}

std.net::udp_socket pass_udp_socket(std.net::udp_socket value) {
    std.net::udp_socket local = move value;
    return move local;
}

std.net::unix_listener pass_unix_listener(std.net::unix_listener value) {
    std.net::unix_listener local = move value;
    return move local;
}

std.net::unix_stream pass_unix_stream(std.net::unix_stream value) {
    std.net::unix_stream local = move value;
    return move local;
}

std.net::unix_datagram pass_unix_datagram(std.net::unix_datagram value) {
    std.net::unix_datagram local = move value;
    return move local;
}

std.signal::listener pass_signal_listener(std.signal::listener value) {
    std.signal::listener local = move value;
    return move local;
}

std.process::child pass_process_child(std.process::child value) {
    std.process::child local = move value;
    return move local;
}

std.process::command pass_process_command(std.process::command value) {
    std.process::command local = move value;
    return move local;
}

std.process::spawn_result pass_process_spawn_result(std.process::spawn_result value) {
    std.process::spawn_result local = move value;
    return move local;
}

std.process::wait_result pass_process_wait_result(std.process::wait_result value) {
    std.process::wait_result local = move value;
    return move local;
}

std.sync::channel<i32> pass_channel(std.sync::channel<i32> value) {
    std.sync::channel<i32> local = move value;
    return move local;
}

std.sync::receiver<i32> pass_receiver(std.sync::receiver<i32> value) {
    std.sync::receiver<i32> local = move value;
    return move local;
}

std.sync::sender<i32> pass_sender(std.sync::sender<i32> value) {
    std.sync::sender<i32> local = move value;
    return move local;
}

std.sync::sync_channel<i32> pass_sync_channel(std.sync::sync_channel<i32> value) {
    std.sync::sync_channel<i32> local = move value;
    return move local;
}

std.sync::sync_sender<i32> pass_sync_sender(std.sync::sync_sender<i32> value) {
    std.sync::sync_sender<i32> local = move value;
    return move local;
}

void drop_write_all_result(std.io::write_all_result value) {
    drop value;
}

void drop_read_result(std.io::read_result value) {
    drop value;
}

void drop_io_write_result(std.io::write_result value) {
    drop value;
}

void drop_shared_write_result(std.io::shared_write_result value) {
    drop value;
}

void drop_write_file_result(std.fs::write_file_result value) {
    drop value;
}

void drop_once(std.sync::once value) {
    drop value;
}

arc payload pass_arc(arc payload value) {
    arc payload local = move value;
    return move local;
}

async void consume_arc(arc payload value) {
    return;
}

async void forward_arc(arc payload value) {
    try {
        task<void> operation = consume_arc(move value);
        await move operation;
    } catch (std.async::start_error error) {
        error as void;
        drop value;
    }
}

i32 main() {
    return 0;
}

std.json::decoder<i32> pass_json_decoder(std.json::decoder<i32> value) {
    std.json::decoder<i32> local = move value;
    return move local;
}
void drop_json_decoder(std.json::decoder<i32> value) {
    drop value;
}

std.json::reader<std.io::input> pass_json_reader(std.json::reader<std.io::input> value) {
    std.json::reader<std.io::input> local = move value;
    return move local;
}
void drop_json_reader(std.json::reader<std.io::input> value) { drop value; }
std.json::detached<std.io::input> pass_json_detached(std.json::detached<std.io::input> value) {
    std.json::detached<std.io::input> local = move value;
    return move local;
}
void drop_json_detached(std.json::detached<std.io::input> value) { drop value; }

std.net::tcp_stream pass_tcp_stream(std.net::tcp_stream value) {
    std.net::tcp_stream local = move value;
    return move local;
}
void drop_tcp_stream(std.net::tcp_stream value) { drop value; }

// L30: the handles, guards and permits of R-SLIB-ASYNC-0013..0016 and R-LIB-0016.
std.async::mutex<i32> pass_async_mutex(std.async::mutex<i32> value) {
    std.async::mutex<i32> local = move value;
    return move local;
}

std.async::mutex_guard<i32> pass_async_mutex_guard(std.async::mutex_guard<i32> value) {
    std.async::mutex_guard<i32> local = move value;
    return move local;
}

std.async::rw_lock<i32> pass_async_rw_lock(std.async::rw_lock<i32> value) {
    std.async::rw_lock<i32> local = move value;
    return move local;
}

std.async::rw_read_guard<i32> pass_async_rw_read_guard(std.async::rw_read_guard<i32> value) {
    std.async::rw_read_guard<i32> local = move value;
    return move local;
}

std.async::rw_write_guard<i32> pass_async_rw_write_guard(std.async::rw_write_guard<i32> value) {
    std.async::rw_write_guard<i32> local = move value;
    return move local;
}

std.async::semaphore pass_async_semaphore(std.async::semaphore value) {
    std.async::semaphore local = move value;
    return move local;
}

std.async::semaphore_permit pass_async_semaphore_permit(std.async::semaphore_permit value) {
    std.async::semaphore_permit local = move value;
    return move local;
}

std.async::notify pass_async_notify(std.async::notify value) {
    std.async::notify local = move value;
    return move local;
}

std.async::broadcast<i32> pass_async_broadcast(std.async::broadcast<i32> value) {
    std.async::broadcast<i32> local = move value;
    return move local;
}

std.async::broadcast_receiver<i32> pass_async_broadcast_receiver(std.async::broadcast_receiver<i32> value) {
    std.async::broadcast_receiver<i32> local = move value;
    return move local;
}

std.sync::permit<i32> pass_sync_permit(std.sync::permit<i32> value) {
    std.sync::permit<i32> local = move value;
    return move local;
}
