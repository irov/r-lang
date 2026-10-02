module example.preflight.checks;
import example.calculator.common::{Usage};

enum Check { address, path, utf8, boundary, bytes, duration, barrier, reserve, integer, thread, asynchronous, profile, frame, packet };

// A bounded wire frame: a four-byte big-endian byte count followed by UTF-8 data.
@generic<const usize N>
struct Frame { u8[N] data; usize used; };

@generic<const usize N>
@noalloc @nonblocking
void pack(Frame<N>* destination, const u8[] source) throws Usage {
    usize count = len(source);
    throw (N < 4usize || count > N - 4usize || count > 4294967295usize)
        Usage {.message="message exceeds fixed frame capacity"};
    u32 size = count as u32;
    destination->data[0usize] = (size >> 24u32) as u8;
    destination->data[1usize] = (size >> 16u32) as u8;
    destination->data[2usize] = (size >> 8u32) as u8;
    destination->data[3usize] = size as u8;
    for (usize index = 0usize; index < count; index += 1usize) {
        destination->data[index + 4usize] = source[index];
    }
    destination->used = count + 4usize;
}

@generic<const usize N>
@noalloc @nonblocking
u32 frame_crc(const Frame<N>* source) {
    const u8[] data = source->data[0usize..source->used];
    u32 checksum = std.hash::crc32(data);
    return checksum;
}

// Compile the execution policy for the selected service profile.
@if (core::profile is hosted-thread || core::profile is hosted-native-async) {
    @noalloc @nonblocking
    constexpr str execution_model() {
        @if (core::profile is hosted-native-async) { return "threads+tasks"; }
        @else { return "threads"; }
    }
} @else {
    @noalloc @nonblocking
    constexpr str execution_model() { return "single-thread"; }
}

std.string::string report(std.error::error error) throws std.alloc::alloc_error {
    std.error::domain domain = error.domain;
    constexpr str family = "other";
    if (domain == std.error::domain::allocation) { family = "allocation"; }
    if (domain == std.error::domain::async_runtime) { family = "async"; }
    if (domain == std.error::domain::bytes) { family = "bytes"; }
    if (domain == std.error::domain::string) { family = "string"; }
    if (domain == std.error::domain::conversion) { family = "conversion"; }
    if (domain == std.error::domain::time) { family = "time"; }
    if (domain == std.error::domain::filesystem) { family = "filesystem"; }
    if (domain == std.error::domain::network) { family = "network"; }
    if (domain == std.error::domain::threading) { family = "threading"; }
    constexpr str name = error.name();
    std.string::string diagnostic = error.diagnostic();
    return f"domain={family} code={error.code} native={error.native_code} name={name}\n{diagnostic}\n";
}

u8 digit(u8 value) throws Usage {
    if (value >= 48u8 && value <= 57u8) { return (value - 48u8) as u8; }
    if (value >= 65u8 && value <= 70u8) { return (value - 55u8) as u8; }
    if (value >= 97u8 && value <= 102u8) { return (value - 87u8) as u8; }
    throw Usage { .message = "invalid hexadecimal byte" };
}
bytes decode(str text) throws Usage, std.alloc::alloc_error {
    const u8[] input = text;
    usize count = len(input);
    throw (count % 2usize != 0usize) Usage { .message = "hex input needs pairs of digits" };
    bytes output = std.alloc::bytes(count / 2usize, 0u8);
    u8[] storage = output.as_slice_mut();
    for (usize index = 0usize; index < count; index += 2usize) {
        u8 high = digit(input[index]);
        u8 low = digit(input[index + 1usize]);
        storage[index / 2usize] = ((high as u32) * 16u32 + (low as u32)) as u8;
    }
    return move output;
}

i32 probe_thread() { return 42; }
async void probe_task() { return; }

std.string::string check(Check kind, str source, usize amount) throws Usage, std.alloc::alloc_error {
    try {
        switch (kind) {
        case Check::packet:
            throw Usage {.message = "packet inspection requires the async entry"};
        case Check::frame:
            Frame<64usize> frame = {};
            pack(&frame, source);
            u32 checksum = frame_crc(&frame);
            return f"capacity=64 used={frame.used} crc32={checksum}\n";
        case Check::profile:
            constexpr str selected_profile = core::profile_name();
            constexpr str target = core::target_name();
            constexpr str execution = execution_model();
            return f"profile={selected_profile} target={target} execution={execution}\n";
        case Check::address:
            std.net::ip_address address = std.net::parse_ip(source); address as void;
            break;
        case Check::path:
            std.fs::path path = std.fs::path_from_utf8(source); drop path;
            break;
        case Check::utf8:
            bytes input = decode(source);
            const u8[] data = input.as_slice();
            std.string::string text = std.string::from_utf8(data); drop text;
            break;
        case Check::boundary:
            std.string::string text = std.string::from_str(source);
            text.truncate(amount);
            break;
        case Check::bytes:
            bytes data = std.alloc::bytes(8usize, 0u8);
            u8[] storage = data.as_slice_mut();
            usize copied = std.bytes::copy_within(storage, 0usize, amount, 1usize); copied as void;
            break;
        case Check::duration:
            i64 seconds = std.convert::parse_i64(source, 10u32);
            throw (amount > 4294967295usize) Usage { .message = "nanoseconds exceed u32" };
            std.time::duration duration = std.time::duration_from_parts(seconds, amount as u32); duration as void;
            break;
        case Check::barrier:
            throw (amount > 64usize) Usage { .message = "barrier check is limited to 64 participants" };
            std.sync::barrier barrier = std.sync::barrier_new(amount); drop barrier;
            break;
        case Check::reserve:
            usize maximum = ~0usize;
            throw (amount > 4096usize && amount != maximum) Usage { .message = "reserve permits 0..4096 or maximum usize" };
            // The array checks multiplication before attempting to allocate element storage.
            array<u64> storage = std.array::create::<u64>();
            storage.reserve(amount);
            break;
        case Check::integer:
            u32 number = std.convert::parse_u32(source, 10u32); number as void;
            break;
        case Check::thread:
            std.thread::join_handle<i32> worker = std.thread::spawn(probe_thread);
            std.thread::join_result<i32> completion = (move worker).join();
            switch (move completion) {
            case variant std.thread::join_result::returned(move number): break;
            case variant std.thread::join_result::panicked(move failure): throw Usage { .message = "thread probe panicked" };
            }
            break;
        case Check::asynchronous:
            task<void> operation = probe_task();
            (move operation).detach();
            break;
        }
    } catch (std.net::address_error failure) {
        std.error::error error = std.error::from_address(failure);
        std.string::string result = report(error); return move result;
    } catch (std.fs::path_error failure) {
        std.error::error error = std.error::from_path(failure);
        std.string::string result = report(error); return move result;
    } catch (std.string::string_error failure) {
        std.error::error error = std.error::from_string(failure);
        std.string::string result = report(error); return move result;
    } catch (std.string::boundary_error failure) {
        std.error::error error = std.error::from_boundary(failure);
        std.string::string result = report(error); return move result;
    } catch (std.bytes::bytes_error failure) {
        std.error::error error = std.error::from_bytes(failure);
        std.string::string result = report(error); return move result;
    } catch (std.time::duration_error failure) {
        std.error::error error = std.error::from_duration(failure);
        std.string::string result = report(error); return move result;
    } catch (std.sync::barrier_error failure) {
        std.error::error error = std.error::from_barrier(failure);
        std.string::string result = report(error); return move result;
    } catch (std.alloc::alloc_error failure) {
        std.error::error error = std.error::from_alloc(failure);
        std.string::string result = report(error); return move result;
    } catch (std.convert::parse_error failure) {
        std.convert::parse_error_code code = failure.code;
        std.error::error error = std.error::from_parse(failure);
        std.string::string result = report(error);
        if (code == std.convert::parse_error_code::above_maximum) {
            result.append("hint: choose a smaller integer\n");
        }
        return move result;
    } catch (std.thread::thread_error failure) {
        std.error::error error = std.error::from_thread(failure);
        std.string::string result = report(error); return move result;
    } catch (std.async::start_error failure) {
        std.error::error error = std.error::from_async(failure);
        std.string::string result = report(error); return move result;
    }
    return std.string::from_str("ok\n");
}
