module example.tables.main;
import example.tables.model::{Header, Queue, Ping, Data, Packet, FRAME_BYTES, PAYLOAD_BYTES,
                              QUEUE_CAPACITY, crc32, opcode, packet, wrap};

// The frame size is a translation-time value, so the type below has a fixed layout.
struct Frame {
    Header header;
    u8[PAYLOAD_BYTES] payload;
    u32 checksum;
};

enum Operation : u8 { ping = opcode(1u8), data = opcode(2u8), close = opcode(3u8), };

i32 main() {
    Frame frame = {};
    u8[5] text = {104u8, 101u8, 108u8, 108u8, 111u8};
    u32 table_checksum = crc32(&text);
    u32 library_checksum = std.hash::crc32(&text);
    constexpr str name = core::enum_name(Operation::data);
    bool checksum_ok = table_checksum == library_checksum && table_checksum == 0x3610A686u32;
    bool layout_ok = sizeof(Frame) == FRAME_BYTES && FRAME_BYTES == 60usize;
    bool payload_ok = len(frame.payload) == PAYLOAD_BYTES;
    bool capacity_ok = QUEUE_CAPACITY == 1024usize;
    bool operation_ok = len(name) == 4usize;
    Queue queue = {};
    bool queue_ok = len(queue.slots) == QUEUE_CAPACITY && wrap(1030usize) == 6usize;
    Packet<Ping> ping = packet::<Ping>();
    bool ping_ok = len(ping.payload) == 8usize && ping.header.version == 1u8 &&
                   ping.header.opcode == opcode(1u8);
    Packet<Data> data = packet::<Data>();
    bool data_ok = len(data.payload) == PAYLOAD_BYTES && data.header.version == 2u8 &&
                   data.header.opcode == Data::OPCODE;
    if (checksum_ok == false || layout_ok == false || payload_ok == false ||
        capacity_ok == false || operation_ok == false || queue_ok == false || ping_ok == false ||
        data_ok == false) {
        return 1;
    }
    return 0;
}
