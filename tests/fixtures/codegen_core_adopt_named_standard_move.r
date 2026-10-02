module codegen.core_adopt_named_standard_move;

void round_trip_001(raw std.async::broadcast<i32>* pointer) {
    unsafe {
        own std.async::broadcast<i32>* owner = core::adopt(pointer);
        raw std.async::broadcast<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_002(raw std.async::broadcast_receiver<i32>* pointer) {
    unsafe {
        own std.async::broadcast_receiver<i32>* owner = core::adopt(pointer);
        raw std.async::broadcast_receiver<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_003(raw std.async::mutex<i32>* pointer) {
    unsafe {
        own std.async::mutex<i32>* owner = core::adopt(pointer);
        raw std.async::mutex<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_004(raw std.async::mutex_guard<i32>* pointer) {
    unsafe {
        own std.async::mutex_guard<i32>* owner = core::adopt(pointer);
        raw std.async::mutex_guard<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_005(raw std.async::notify* pointer) {
    unsafe {
        own std.async::notify* owner = core::adopt(pointer);
        raw std.async::notify* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_006(raw std.async::rw_lock<i32>* pointer) {
    unsafe {
        own std.async::rw_lock<i32>* owner = core::adopt(pointer);
        raw std.async::rw_lock<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_007(raw std.async::rw_read_guard<i32>* pointer) {
    unsafe {
        own std.async::rw_read_guard<i32>* owner = core::adopt(pointer);
        raw std.async::rw_read_guard<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_008(raw std.async::rw_write_guard<i32>* pointer) {
    unsafe {
        own std.async::rw_write_guard<i32>* owner = core::adopt(pointer);
        raw std.async::rw_write_guard<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_009(raw std.async::semaphore* pointer) {
    unsafe {
        own std.async::semaphore* owner = core::adopt(pointer);
        raw std.async::semaphore* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_010(raw std.async::semaphore_permit* pointer) {
    unsafe {
        own std.async::semaphore_permit* owner = core::adopt(pointer);
        raw std.async::semaphore_permit* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_011(raw std.c::c_string* pointer) {
    unsafe {
        own std.c::c_string* owner = core::adopt(pointer);
        raw std.c::c_string* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_012(raw std.c::handle* pointer) {
    unsafe {
        own std.c::handle* owner = core::adopt(pointer);
        raw std.c::handle* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_013(raw std.c::thread_attachment* pointer) {
    unsafe {
        own std.c::thread_attachment* owner = core::adopt(pointer);
        raw std.c::thread_attachment* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_014(raw std.format::builder* pointer) {
    unsafe {
        own std.format::builder* owner = core::adopt(pointer);
        raw std.format::builder* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_015(raw std.fs::directory* pointer) {
    unsafe {
        own std.fs::directory* owner = core::adopt(pointer);
        raw std.fs::directory* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_016(raw std.fs::directory_entry* pointer) {
    unsafe {
        own std.fs::directory_entry* owner = core::adopt(pointer);
        raw std.fs::directory_entry* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_017(raw std.fs::directory_iter* pointer) {
    unsafe {
        own std.fs::directory_iter* owner = core::adopt(pointer);
        raw std.fs::directory_iter* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_018(raw std.fs::directory_next_result* pointer) {
    unsafe {
        own std.fs::directory_next_result* owner = core::adopt(pointer);
        raw std.fs::directory_next_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_019(raw std.fs::file* pointer) {
    unsafe {
        own std.fs::file* owner = core::adopt(pointer);
        raw std.fs::file* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_020(raw std.fs::path* pointer) {
    unsafe {
        own std.fs::path* owner = core::adopt(pointer);
        raw std.fs::path* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_021(raw std.fs::write_file_result* pointer) {
    unsafe {
        own std.fs::write_file_result* owner = core::adopt(pointer);
        raw std.fs::write_file_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_022(raw std.io::input* pointer) {
    unsafe {
        own std.io::input* owner = core::adopt(pointer);
        raw std.io::input* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_023(raw std.io::output* pointer) {
    unsafe {
        own std.io::output* owner = core::adopt(pointer);
        raw std.io::output* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_024(raw std.io::read_result* pointer) {
    unsafe {
        own std.io::read_result* owner = core::adopt(pointer);
        raw std.io::read_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_025(raw std.io::shared_write_result* pointer) {
    unsafe {
        own std.io::shared_write_result* owner = core::adopt(pointer);
        raw std.io::shared_write_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_026(raw std.io::write_all_result* pointer) {
    unsafe {
        own std.io::write_all_result* owner = core::adopt(pointer);
        raw std.io::write_all_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_027(raw std.io::write_result* pointer) {
    unsafe {
        own std.io::write_result* owner = core::adopt(pointer);
        raw std.io::write_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_028(raw std.json::decoder<i32>* pointer) {
    unsafe {
        own std.json::decoder<i32>* owner = core::adopt(pointer);
        raw std.json::decoder<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_029(raw std.json::detached<std.io::input>* pointer) {
    unsafe {
        own std.json::detached<std.io::input>* owner = core::adopt(pointer);
        raw std.json::detached<std.io::input>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_030(raw std.json::error* pointer) {
    unsafe {
        own std.json::error* owner = core::adopt(pointer);
        raw std.json::error* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_031(raw std.json::number* pointer) {
    unsafe {
        own std.json::number* owner = core::adopt(pointer);
        raw std.json::number* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_032(raw std.json::reader<std.io::input>* pointer) {
    unsafe {
        own std.json::reader<std.io::input>* owner = core::adopt(pointer);
        raw std.json::reader<std.io::input>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_033(raw std.json::value* pointer) {
    unsafe {
        own std.json::value* owner = core::adopt(pointer);
        raw std.json::value* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_034(raw std.net::tcp_connection* pointer) {
    unsafe {
        own std.net::tcp_connection* owner = core::adopt(pointer);
        raw std.net::tcp_connection* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_035(raw std.net::tcp_listener* pointer) {
    unsafe {
        own std.net::tcp_listener* owner = core::adopt(pointer);
        raw std.net::tcp_listener* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_036(raw std.net::tcp_read_result* pointer) {
    unsafe {
        own std.net::tcp_read_result* owner = core::adopt(pointer);
        raw std.net::tcp_read_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_037(raw std.net::tcp_stream* pointer) {
    unsafe {
        own std.net::tcp_stream* owner = core::adopt(pointer);
        raw std.net::tcp_stream* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_038(raw std.net::tcp_write_all_result* pointer) {
    unsafe {
        own std.net::tcp_write_all_result* owner = core::adopt(pointer);
        raw std.net::tcp_write_all_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_039(raw std.net::tcp_write_result* pointer) {
    unsafe {
        own std.net::tcp_write_result* owner = core::adopt(pointer);
        raw std.net::tcp_write_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_040(raw std.net::udp_receive_result* pointer) {
    unsafe {
        own std.net::udp_receive_result* owner = core::adopt(pointer);
        raw std.net::udp_receive_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_041(raw std.net::udp_send_result* pointer) {
    unsafe {
        own std.net::udp_send_result* owner = core::adopt(pointer);
        raw std.net::udp_send_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_042(raw std.net::udp_socket* pointer) {
    unsafe {
        own std.net::udp_socket* owner = core::adopt(pointer);
        raw std.net::udp_socket* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_043(raw std.net::unix_datagram* pointer) {
    unsafe {
        own std.net::unix_datagram* owner = core::adopt(pointer);
        raw std.net::unix_datagram* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_044(raw std.net::unix_listener* pointer) {
    unsafe {
        own std.net::unix_listener* owner = core::adopt(pointer);
        raw std.net::unix_listener* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_045(raw std.net::unix_stream* pointer) {
    unsafe {
        own std.net::unix_stream* owner = core::adopt(pointer);
        raw std.net::unix_stream* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_046(raw std.process::child* pointer) {
    unsafe {
        own std.process::child* owner = core::adopt(pointer);
        raw std.process::child* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_047(raw std.process::command* pointer) {
    unsafe {
        own std.process::command* owner = core::adopt(pointer);
        raw std.process::command* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_048(raw std.process::spawn_result* pointer) {
    unsafe {
        own std.process::spawn_result* owner = core::adopt(pointer);
        raw std.process::spawn_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_049(raw std.process::wait_result* pointer) {
    unsafe {
        own std.process::wait_result* owner = core::adopt(pointer);
        raw std.process::wait_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_050(raw std.secret::buffer* pointer) {
    unsafe {
        own std.secret::buffer* owner = core::adopt(pointer);
        raw std.secret::buffer* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_051(raw std.signal::listener* pointer) {
    unsafe {
        own std.signal::listener* owner = core::adopt(pointer);
        raw std.signal::listener* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_052(raw std.string::from_bytes_result* pointer) {
    unsafe {
        own std.string::from_bytes_result* owner = core::adopt(pointer);
        raw std.string::from_bytes_result* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_053(raw std.string::string* pointer) {
    unsafe {
        own std.string::string* owner = core::adopt(pointer);
        raw std.string::string* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_054(raw std.sync::barrier* pointer) {
    unsafe {
        own std.sync::barrier* owner = core::adopt(pointer);
        raw std.sync::barrier* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_055(raw std.sync::channel<i32>* pointer) {
    unsafe {
        own std.sync::channel<i32>* owner = core::adopt(pointer);
        raw std.sync::channel<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_056(raw std.sync::condvar* pointer) {
    unsafe {
        own std.sync::condvar* owner = core::adopt(pointer);
        raw std.sync::condvar* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_057(raw std.sync::once* pointer) {
    unsafe {
        own std.sync::once* owner = core::adopt(pointer);
        raw std.sync::once* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_058(raw std.sync::permit<i32>* pointer) {
    unsafe {
        own std.sync::permit<i32>* owner = core::adopt(pointer);
        raw std.sync::permit<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_059(raw std.sync::receiver<i32>* pointer) {
    unsafe {
        own std.sync::receiver<i32>* owner = core::adopt(pointer);
        raw std.sync::receiver<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_060(raw std.sync::sender<i32>* pointer) {
    unsafe {
        own std.sync::sender<i32>* owner = core::adopt(pointer);
        raw std.sync::sender<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_061(raw std.sync::sync_channel<i32>* pointer) {
    unsafe {
        own std.sync::sync_channel<i32>* owner = core::adopt(pointer);
        raw std.sync::sync_channel<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_062(raw std.sync::sync_sender<i32>* pointer) {
    unsafe {
        own std.sync::sync_sender<i32>* owner = core::adopt(pointer);
        raw std.sync::sync_sender<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_063(raw std.thread::panic_report* pointer) {
    unsafe {
        own std.thread::panic_report* owner = core::adopt(pointer);
        raw std.thread::panic_report* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_064(raw std.thread::thread* pointer) {
    unsafe {
        own std.thread::thread* owner = core::adopt(pointer);
        raw std.thread::thread* returned = core::release(move owner);
        returned as void;
    }
}

i32 main() {
    return 0;
}
