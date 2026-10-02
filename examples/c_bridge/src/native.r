module example.c_bridge.native;
import example.calculator.common::{Usage};

@link(name = "system.libc", kind = "system")
@header("string.h")
extern "C" {
    @safety("BRIDGE-STRLEN", "The argument is a live NUL-terminated std.c string")
    c_size strlen(raw const c_char* text);
}

struct Packet { u8[256] data; usize length; constexpr str failure; };

@callback
@safety("BRIDGE-PACKET-DESTROY", "The pointer uniquely owns a released Packet allocation; the runtime is live")
extern "C" void destroy_packet(raw void* pointer) {
    unsafe {
        own Packet* packet = core::adopt(pointer as raw Packet*);
        drop packet;
    }
}

// The caller supplies a live writable Packet with length at most 256. Nothing owns the packet
// on this side of the callback, so its payload is read and written through raw-parts views
// whose regions end in this function. A payload that is not UTF-8 is left unchanged and
// reported through the failure text of the packet.
unsafe c_int uppercase_bytes(raw void* pointer) {
    unsafe {
        raw Packet* packet = pointer as raw Packet*;
        usize length = packet->length;
        bool bounded = length <= 256usize;
        core::assume(bounded);
        raw const u8* input = &packet->data[0usize] as raw const u8*;
        const u8[] text = core::slice_from_raw_parts(input, length);
        if (std.utf8::is_valid(text) == false) {
            packet->failure = "bridge packet is not UTF-8";
            return 1i32 as c_int;
        }
        raw u8* address = &packet->data[0usize] as raw u8*;
        u8[] bytes = core::slice_from_raw_parts_mut(address, length);
        usize byte_count = len(bytes);
        for (usize index = 0usize; index < byte_count; index += 1usize) {
            u8 value = bytes[index];
            if (value >= 97u8 && value <= 122u8) { bytes[index] = (value - 32u8) as u8; }
        }
    }
    return 0i32 as c_int;
}

@callback
@safety("BRIDGE-PACKET-UPPER", "The pointer addresses a uniquely writable live Packet with length at most 256; the runtime is live")
extern "C" c_int uppercase(raw void* pointer) {
    unsafe { return uppercase_bytes(pointer); }
}

// The handle takes over the allocation and the duty to destroy it.
std.c::handle adopt_packet(own Packet* value) {
    unsafe {
        raw Packet* pointer = core::release(move value);
        return std.c::adopt_handle(pointer as raw void*, destroy_packet);
    }
}

// The payload of the packet that a handle owns, as views anchored to the handle (Core
// R-UNSAFE-0008): the packet lives as long as the handle, and while a view is live the handle
// can be neither released nor dropped, and the exclusive view is its only access.
const u8[] packet_bytes(const std.c::handle* handle) {
    unsafe {
        raw const Packet* packet = std.c::handle_pointer(handle) as raw const Packet*;
        raw const u8* data = &packet->data[0usize] as raw const u8*;
        return core::slice_from_raw_parts_in(handle, data, packet->length);
    }
}

u8[] packet_bytes_mut(std.c::handle* handle) {
    unsafe {
        raw Packet* packet = std.c::handle_pointer(handle) as raw Packet*;
        raw u8* data = &packet->data[0usize] as raw u8*;
        return core::slice_from_raw_parts_in_mut(handle, data, packet->length);
    }
}

std.c::handle create_packet(str text) throws Usage {
    const u8[] source = text;
    usize length = len(source);
    throw (length > 256usize) Usage { .message = "bridge text is limited to 256 UTF-8 bytes" };
    std.c::handle handle = adopt_packet(new Packet { .data = {}, .length = length, .failure = "" });
    {
        u8[] destination = packet_bytes_mut(&handle);
        for (usize index = 0usize; index < len(destination); index += 1usize) {
            destination[index] = source[index];
        }
    }
    return move handle;
}

std.string::string transform(str text, bool release) throws Usage, core::utf8_error, std.alloc::alloc_error {
    std.c::handle handle = create_packet(text);
    unsafe {
        raw void* pointer = handle.pointer();
        raw fn(raw void*) -> c_int operation = uppercase;
        c_int status = operation(pointer);
        if (status != 0i32 as c_int) {
            raw const Packet* failed = pointer as raw const Packet*;
            throw Usage { .message = failed->failure };
        }
    }
    str validated = core::validate_utf8(packet_bytes(&handle));
    std.string::string result = std.string::from_str(validated);
    if (release == true) {
        raw void* pointer = (move handle).release();
        unsafe {
            raw fn(raw void*) -> void destructor = destroy_packet;
            destructor(pointer);
        }
    } else { drop handle; }
    return move result;
}

std.string::string inspect(str text) throws std.c::string_error, std.alloc::alloc_error {
    std.c::c_string owned = std.c::string_from_str(text);
    const c_char[] storage = owned.as_slice();
    str validated = std.c::validate_utf8(storage);
    std.string::string copied = std.c::copy_utf8(storage);
    usize storage_size = len(storage);
    usize payload_size = len(validated);
    unsafe {
        raw const c_char* pointer = owned.as_ptr();
        c_size length = strlen(pointer);
        usize native_length = length as usize;
    return f"storage={storage_size} bytes={payload_size} strlen={native_length} text={copied}\n";
    }
}

std.string::string target() throws std.alloc::alloc_error {
    std.c::target_info info = std.c::target();
    bool libc = std.c::link_available("system.libc");
    return f"pointer_bits={info.pointer_bits} c_wint={info.c_wint_available} long_double={info.c_long_double_available} async={info.hosted_native_async} libc={libc}\n";
}

u8 hex_digit(u8 value) throws Usage {
    if (value >= 48u8 && value <= 57u8) { return (value - 48u8) as u8; }
    if (value >= 65u8 && value <= 70u8) { return (value - 55u8) as u8; }
    if (value >= 97u8 && value <= 102u8) { return (value - 87u8) as u8; }
    throw Usage { .message = "hex input contains a non-hexadecimal byte" };
}

bytes decode_hex(str text) throws Usage, std.alloc::alloc_error {
    const u8[] input = text;
    usize length = len(input);
    throw (length % 2usize != 0usize) Usage { .message = "hex input needs an even number of digits" };
    bytes output = std.alloc::bytes(length / 2usize, 0u8);
    for (usize index = 0usize; index < length; index += 2usize) {
        u8 high = hex_digit(input[index]);
        u8 low = hex_digit(input[index + 1usize]);
        u32 combined = (high as u32) * 16u32 + (low as u32);
        o<u8*> slot = output.get_mut(index / 2usize);
        switch (move slot) {
        case variant o::some(value): **value = combined as u8; break;
        case variant o::none: throw Usage { .message = "hex output index is out of range" };
        }
    }
    return move output;
}

// The bytes of a view as C characters of the same storage, a view anchored to it: c_char has
// the size and alignment of u8, and the view reads nothing beyond the bytes.
const c_char[] c_chars_of(const u8[] input) {
    usize length = len(input);
    unsafe {
        raw const c_char*? pointer = null;
        if (length != 0usize) {
            raw const u8* bytes = &input[0usize] as raw const u8*;
            raw const void* storage = bytes as raw const void*;
            pointer = storage as raw const c_char*;
        }
        return core::slice_from_raw_parts_in(input, pointer, length);
    }
}

std.string::string inspect_bytes(const u8[] input) throws std.c::string_error, std.alloc::alloc_error {
    const c_char[] storage = c_chars_of(input);
    str validated = std.c::validate_utf8(storage);
    std.string::string copied = std.c::copy_utf8(storage);
    usize bytes = len(validated);
    return f"bytes={bytes} text={copied}\n";
}

// An explicit attachment owns its floating environment. Duplicate attachment is rejected.
std.string::string attachment_status() throws std.alloc::alloc_error {
    unsafe {
        try {
            std.c::thread_attachment attachment = std.c::attach_thread();
            constexpr str duplicate = "unexpected success";
            try {
                std.c::thread_attachment second = std.c::attach_thread();
                (move second).detach();
            } catch (std.c::runtime_error failure) {
                std.error::error error = failure.as_error();
                duplicate = error.name();
            }
            (move attachment).detach();
            return f"attached and detached; duplicate={duplicate}\n";
        } catch (std.c::runtime_error failure) {
            std.error::error error = failure.as_error();
            constexpr str name = error.name();
            return f"attachment rejected: {name}\n";
        }
    }
}
