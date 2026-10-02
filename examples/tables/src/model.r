module example.tables.model;

// Ordinary functions with known arguments and no external effects run during translation
// (Core R-FUNC-0023, R-EXPR-0032). Nothing marks them: the compiler proves it from the body.

// One CRC-32 (IEEE 802.3) table entry: eight shift-and-xor rounds of the reflected polynomial.
u32 crc32_entry(u32 index) {
    u32 value = index;
    for (i32 round = 0; round < 8; round++) {
        if ((value & 1u32) != 0u32) {
            value = 0xEDB88320u32 ^ (value >> 1u32);
        } else {
            value = value >> 1u32;
        }
    }
    return value;
}

u32[256] crc32_table() {
    u32[256] table = {};
    for (u32 index in 0u32..256u32) {
        table[index] = crc32_entry(index);
    }
    return table;
}

// The wire header of a frame; its size follows the target layout rules.
struct Header {
    u32 magic;
    u16 length;
    u8 version;
    u8 opcode;
};

usize frame_bytes(usize payload) {
    return sizeof(Header) + payload + sizeof(u32);
}

// A configuration value checked at translation time: an invalid capacity stops the build.
usize checked_capacity(usize requested) {
    if (requested == 0usize || requested > 65536usize) {
        panic("queue capacity must be within 1..65536");
    }
    if ((requested & (requested - 1usize)) != 0usize) {
        panic("queue capacity must be a power of two");
    }
    return requested;
}

// Operation codes carry the protocol version in their high bits.
u8 opcode(u8 operation) {
    u8 code = operation;
    code |= 0x40u8;
    return code;
}

const usize PAYLOAD_BYTES = 48usize;
const usize FRAME_BYTES = frame_bytes(PAYLOAD_BYTES);
const usize QUEUE_CAPACITY = checked_capacity(1024usize);
const u32[256] CRC32_TABLE = crc32_table();

// A constant condition selects declarations during translation (Core R-META-0002): slots that
// fit in one 4 KiB page stay inline, a larger queue would keep them on the heap. Only the
// selected branch is checked and generated.
@if (QUEUE_CAPACITY * sizeof(u32) <= 4096usize) {
    struct Queue { u32[QUEUE_CAPACITY] slots; usize head; };
} @else {
    struct Queue { array<u32> slots; usize head; };
}

// A power-of-two capacity wraps with a mask, any other one with a division.
usize wrap(usize index) {
    @if ((QUEUE_CAPACITY & (QUEUE_CAPACITY - 1usize)) == 0usize) {
        return index & (QUEUE_CAPACITY - 1usize);
    } @else {
        return index % QUEUE_CAPACITY;
    }
}

// A message kind fixes its operation code and payload size (Core R-TYPE-0050). Generic code
// reads them as `M::OPCODE` and `M::PAYLOAD` for each kind during translation.
trait Message {
    const u8 OPCODE;
    const usize PAYLOAD;
    const bool ACKNOWLEDGED = false;
};

struct Ping { u8 unused; };
struct Data { u8 unused; };

impl Message for Ping {
    const u8 OPCODE = opcode(1u8);
    const usize PAYLOAD = 8usize;
};

impl Message for Data {
    const u8 OPCODE = opcode(2u8);
    const usize PAYLOAD = PAYLOAD_BYTES;
    const bool ACKNOWLEDGED = true;
};

@generic<M: Message>
struct Packet {
    Header header;
    u8[M::PAYLOAD] payload;
};

@generic<M: Message>
Packet<M> packet() {
    Packet<M> result = {};
    result.header.opcode = M::OPCODE;
    result.header.length = M::PAYLOAD as u16;
    @if (M::ACKNOWLEDGED == true) {
        result.header.version = 2u8;
    } @else {
        result.header.version = 1u8;
    }
    return result;
}

u32 crc32(const u8[] bytes) {
    u32 value = 0xFFFFFFFFu32;
    for (usize index = 0usize; index < len(bytes); index++) {
        u32 slot = (value ^ (bytes[index] as u32)) & 0xFFu32;
        value = CRC32_TABLE[slot as usize] ^ (value >> 8u32);
    }
    return value ^ 0xFFFFFFFFu32;
}
