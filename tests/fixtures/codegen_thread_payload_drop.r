module test.codegen.thread_payload_drop;
struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }
error Message { Empty, Data(Tracked), };
drop(Message* self) {
    switch (*self) {
    case variant Message::Empty: break;
    case variant Message::Data(value): if (*((*value).data) != 22) { panic("invalid payload drop"); } break;
    }
}
i32 run_struct(Tracked value) { return *(value.data); }
i32 run_enum(Message value) {
    switch (value) {
    case variant Message::Empty: return 0;
    case variant Message::Data(item): return *((*item).data);
    }
}
i32 main() {
    try {
        Tracked first = {.data=new i32(20)};
        std.thread::join_handle<i32> a = std.thread::spawn(run_struct, move first);
        std.thread::join_result<i32> x = (move a).join();
        switch (move x) {
        case variant std.thread::join_result::returned(move value): if (value != 20) { return 1; } break;
        case variant std.thread::join_result::panicked(move report): return 2;
        }
        Message second = Message::Data(Tracked {.data=new i32(22)});
        std.thread::join_handle<i32> b = std.thread::spawn(run_enum, move second);
        std.thread::join_result<i32> y = (move b).join();
        switch (move y) {
        case variant std.thread::join_result::returned(move value): i32 selected = value == 22 ? 0 : 3; return selected;
        case variant std.thread::join_result::panicked(move report): return 4;
        }
    } catch (std.thread::thread_error failure) { return 5; }
}
