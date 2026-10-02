module codegen.core_adopt_compiler_specialized_move;

void round_trip_001(raw core::atomic_compare_exchange_result<i32>* pointer) {
    unsafe {
        own core::atomic_compare_exchange_result<i32>* owner = core::adopt(pointer);
        raw core::atomic_compare_exchange_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_002(raw std.alloc::new_error<i32>* pointer) {
    unsafe {
        own std.alloc::new_error<i32>* owner = core::adopt(pointer);
        raw std.alloc::new_error<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_003(raw std.arc::try_unwrap_result<i32>* pointer) {
    unsafe {
        own std.arc::try_unwrap_result<i32>* owner = core::adopt(pointer);
        raw std.arc::try_unwrap_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_004(raw std.array::push_error<i32>* pointer) {
    unsafe {
        own std.array::push_error<i32>* owner = core::adopt(pointer);
        raw std.array::push_error<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_005(raw std.async::broadcast_result<i32>* pointer) {
    unsafe {
        own std.async::broadcast_result<i32>* owner = core::adopt(pointer);
        raw std.async::broadcast_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_006(raw std.dict::insert_error<i32, i64>* pointer) {
    unsafe {
        own std.dict::insert_error<i32, i64>* owner = core::adopt(pointer);
        raw std.dict::insert_error<i32, i64>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_007(raw std.list::push_error<i32>* pointer) {
    unsafe {
        own std.list::push_error<i32>* owner = core::adopt(pointer);
        raw std.list::push_error<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_008(raw std.rc::try_unwrap_result<i32>* pointer) {
    unsafe {
        own std.rc::try_unwrap_result<i32>* owner = core::adopt(pointer);
        raw std.rc::try_unwrap_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_009(raw std.sync::mutex<i32>* pointer) {
    unsafe {
        own std.sync::mutex<i32>* owner = core::adopt(pointer);
        raw std.sync::mutex<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_010(raw std.sync::once_lock<i32>* pointer) {
    unsafe {
        own std.sync::once_lock<i32>* owner = core::adopt(pointer);
        raw std.sync::once_lock<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_011(raw std.sync::recv_result<i32>* pointer) {
    unsafe {
        own std.sync::recv_result<i32>* owner = core::adopt(pointer);
        raw std.sync::recv_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_012(raw std.sync::reserve_result<i32>* pointer) {
    unsafe {
        own std.sync::reserve_result<i32>* owner = core::adopt(pointer);
        raw std.sync::reserve_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_013(raw std.sync::rw_lock<i32>* pointer) {
    unsafe {
        own std.sync::rw_lock<i32>* owner = core::adopt(pointer);
        raw std.sync::rw_lock<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_014(raw std.sync::send_result<i32>* pointer) {
    unsafe {
        own std.sync::send_result<i32>* owner = core::adopt(pointer);
        raw std.sync::send_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_015(raw std.sync::set_result<i32>* pointer) {
    unsafe {
        own std.sync::set_result<i32>* owner = core::adopt(pointer);
        raw std.sync::set_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_016(raw std.sync::try_recv_result<i32>* pointer) {
    unsafe {
        own std.sync::try_recv_result<i32>* owner = core::adopt(pointer);
        raw std.sync::try_recv_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_017(raw std.sync::try_reserve_result<i32>* pointer) {
    unsafe {
        own std.sync::try_reserve_result<i32>* owner = core::adopt(pointer);
        raw std.sync::try_reserve_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_018(raw std.sync::try_send_result<i32>* pointer) {
    unsafe {
        own std.sync::try_send_result<i32>* owner = core::adopt(pointer);
        raw std.sync::try_send_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_019(raw std.thread::join_handle<i32>* pointer) {
    unsafe {
        own std.thread::join_handle<i32>* owner = core::adopt(pointer);
        raw std.thread::join_handle<i32>* returned = core::release(move owner);
        returned as void;
    }
}

void round_trip_020(raw std.thread::join_result<i32>* pointer) {
    unsafe {
        own std.thread::join_result<i32>* owner = core::adopt(pointer);
        raw std.thread::join_result<i32>* returned = core::release(move owner);
        returned as void;
    }
}

i32 main() {
    return 0;
}
