module std.net;

/* R-SLIB-NET-0011: the R part of std.net, loaded by `import std.net;`: connection by name with
   the addresses tried in order. The operations take no deadline argument; the deadline of an
   enclosing deadline block bounds every resolution and connection attempt (Core R-STMT-0019). */

protected std.net::net_error net_failure(std.net::error_code code) {
    return std.net::net_error {.code = code, .native_code = 0i64};
}

/* Whether a failed attempt ends the whole connection: cancellation and the deadline apply to
   every later candidate as well. */
protected bool final_failure(std.net::error_code code) {
    return code == std.net::error_code::cancelled || code == std.net::error_code::timed_out;
}

/* The first candidate, in order, that accepts a TCP connection; when none does, the error of
   the last attempt. */
async std.net::tcp_stream tcp_connect_any(array<std.net::socket_address> candidates)
    throws std.net::net_error, std.async::start_error {
    usize total = len(candidates);
    throw (total == 0usize) net_failure(std.net::error_code::invalid_address);
    std.net::net_error last = std.net::net_error {.code = std.net::error_code::other, .native_code = 0i64};
    for (usize index = 0usize; index < total; index += 1usize) {
        std.net::socket_address candidate = candidates[index];
        try {
            return await std.net::tcp_connect(candidate);
        } catch (std.net::net_error failure) {
            throw (final_failure(failure.code) == true) failure;
            last = failure;
        }
    }
    throw last;
}

/* The addresses of host for port, of either family in resolver order, connected by
   tcp_connect_any. */
@scoped
async std.net::tcp_stream tcp_connect_name(str host, u16 port)
    throws std.net::net_error, std.async::start_error {
    array<std.net::socket_address> addresses =
        await std.net::resolve(host, port, std.net::family::any);
    return await tcp_connect_any(move addresses);
}

/* The value of a decimal or hexadecimal digit, or 16 for another byte. */
protected u32 digit_value(u8 value, bool hexadecimal) {
    u32 code = value as u32;
    if (code >= 48u32 && code <= 57u32) { return code - 48u32; }
    if (hexadecimal == true && code >= 97u32 && code <= 102u32) { return code - 87u32; }
    if (hexadecimal == true && code >= 65u32 && code <= 70u32) { return code - 55u32; }
    return 16u32;
}

/* Appends the four bytes of dotted IPv4 text[start..end]. */
protected void append_v4(bytes* out, const u8[] text, usize start, usize end) throws std.alloc::alloc_error {
    u32 value = 0u32;
    for (usize index = start; index <= end; index += 1usize) {
        if (index == end || text[index] == 46u8) {
            std.bytes::append_u8(out, value as u8);
            value = 0u32;
            continue;
        }
        value = value * 10u32 + digit_value(text[index], false);
    }
}

/* Appends the 16-bit groups of IPv6 text[start..end] separated by ':'; a final group with dots
   is an IPv4 tail of two groups. Returns the number of groups. */
protected usize append_groups(bytes* out, const u8[] text, usize start, usize end)
    throws std.alloc::alloc_error {
    usize groups = 0usize;
    usize first = start;
    while (first < end) {
        usize stop = first;
        bool dotted = false;
        while (stop < end && text[stop] != 58u8) {
            if (text[stop] == 46u8) { dotted = true; }
            stop += 1usize;
        }
        if (dotted == true) {
            append_v4(out, text, first, stop);
            groups += 2usize;
        } else {
            u32 value = 0u32;
            for (usize index = first; index < stop; index += 1usize) {
                value = value * 16u32 + digit_value(text[index], true);
            }
            std.bytes::append_u8(out, (value >> 8u32) as u8);
            std.bytes::append_u8(out, (value & 255u32) as u8);
            groups += 1usize;
        }
        first = stop + 1usize;
    }
    return groups;
}

/* R-SLIB-NET-0018: the 4 bytes of an IPv4 address or the 16 bytes of an IPv6 address, in
   network order. */
bytes ip_octets(std.net::ip_address value) throws std.alloc::alloc_error {
    std.string::string text = std.net::format_ip(value);
    const u8[] characters = text.as_bytes();
    bytes out = std.bytes::with_capacity(16usize);
    usize colon = len(characters);
    for (usize index = 0usize; index < len(characters); index += 1usize) {
        if (characters[index] == 58u8 && colon == len(characters)) { colon = index; }
    }
    if (colon == len(characters)) {
        append_v4(&out, characters, 0usize, len(characters));
        return move out;
    }
    usize gap = len(characters);
    for (usize index = 0usize; index + 1usize < len(characters); index += 1usize) {
        if (characters[index] == 58u8 && characters[index + 1usize] == 58u8 && gap == len(characters)) {
            gap = index;
        }
    }
    if (gap == len(characters)) {
        usize all = append_groups(&out, characters, 0usize, len(characters));
        all as void;
        return move out;
    }
    bytes tail = std.bytes::with_capacity(16usize);
    usize head_groups = append_groups(&out, characters, 0usize, gap);
    usize tail_groups = append_groups(&tail, characters, gap + 2usize, len(characters));
    usize zeros = 8usize - head_groups - tail_groups;
    for (usize index = 0usize; index < zeros * 2usize; index += 1usize) {
        std.bytes::append_u8(&out, 0u8);
    }
    std.bytes::append(&out, tail.as_slice());
    return move out;
}
