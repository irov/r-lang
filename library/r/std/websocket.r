module std.websocket;
import std.stream;
import std.http;
import std.tls;
import std.hash;
import std.encoding;
import std.text;

/* R-SLIB-WS-0001: why a WebSocket failed. */
@derive(format)
enum error_code {
    handshake_failed,
    protocol_error,
    invalid_data,
    message_too_large,
    closed,
};

/* R-SLIB-WS-0001: a failure of a WebSocket. */
error websocket_error { error_code code; };

protected websocket_error failure(error_code code) { return websocket_error {.code = code}; }

/* R-SLIB-WS-0003: the kinds of message: text, binary data and the close of the peer. */
@derive(format)
enum message_kind { text, binary, close };

/* R-SLIB-WS-0003: one message. The data of a text message is UTF-8; a close message carries the
   status code of the peer, 1005 when it sent none, and its reason as data. */
struct message { message_kind kind; bytes data; u16 code; };

/* R-SLIB-WS-0003: the data of a message as text; data that is not UTF-8 is "". */
str message::text(const message* this) {
    try {
        return core::validate_utf8(this->data.as_slice());
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    return "";
}

protected const constexpr str accept_guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

/* R-SLIB-WS-0002: the Sec-WebSocket-Accept value of a Sec-WebSocket-Key value: the base64 of the
   SHA-1 of the key followed by the GUID of RFC 6455. */
std.string::string accept_key(str key) throws std.alloc::alloc_error {
    bytes joined = {};
    std.bytes::append(&joined, key);
    std.bytes::append(&joined, accept_guid);
    std.hash::sha1_digest digest = std.hash::sha1(joined.as_slice());
    return std.encoding::encode_base64(&digest.bytes);
}

/* R-SLIB-WS-0004: one frame in the wire form of RFC 6455 section 5.2 with the opcode, the FIN
   bit and the payload; masked frames carry a random masking key. */
bytes encode_frame(u8 opcode, bool final, const u8[] payload, bool masked) throws std.alloc::alloc_error {
    usize size = len(payload);
    bytes out = std.bytes::with_capacity(size + 14usize);
    u32 first = (opcode as u32) & 15u32;
    if (final == true) { first |= 128u32; }
    std.bytes::append_u8(&out, first as u8);
    u32 mask_bit = 0u32;
    if (masked == true) { mask_bit = 128u32; }
    if (size < 126usize) {
        std.bytes::append_u8(&out, (mask_bit | (size as u32)) as u8);
    } else {
        if (size < 65536usize) {
            std.bytes::append_u8(&out, (mask_bit | 126u32) as u8);
            std.bytes::append_u8(&out, ((size >> 8usize) & 255usize) as u8);
            std.bytes::append_u8(&out, (size & 255usize) as u8);
        } else {
            std.bytes::append_u8(&out, (mask_bit | 127u32) as u8);
            u64 wide = size as u64;
            for (u64 shift = 56u64; shift > 0u64; shift -= 8u64) {
                std.bytes::append_u8(&out, ((wide >> shift) & 255u64) as u8);
            }
            std.bytes::append_u8(&out, (wide & 255u64) as u8);
        }
    }
    if (masked == false) {
        std.bytes::append(&out, payload);
        return move out;
    }
    u8[4] key = {};
    std.random::fill(&key);
    std.bytes::append(&out, &key);
    for (usize index = 0usize; index < size; index += 1usize) {
        std.bytes::append_u8(&out, ((payload[index] as u32) ^ (key[index & 3usize] as u32)) as u8);
    }
    return move out;
}

/* What receive keeps between calls: the bytes read and not yet decoded, buffered[start..], and
   whether the close of the peer or a failure ended reading. */
protected struct reader_state { bytes buffered; usize start; bool finished; };

/* Whether a close frame has been sent. */
protected struct writer_state { bool closed; };

/* R-SLIB-WS-0003: one end of a WebSocket connection. One task may receive while others send:
   receiving and sending each take their own lock. A client masks the frames it sends. */
struct websocket {
    protected own dyn(std.stream::Stream)* transport;
    protected std.async::mutex<reader_state> reading;
    protected std.async::mutex<writer_state> writing;
    protected bool client;
    protected usize max_message;
};

/* R-SLIB-WS-0003: a WebSocket over a connection that has switched to it; the bytes read with the
   head of the handshake are its first bytes. */
websocket websocket::create(std.http::upgraded connection, bool client) throws std.alloc::alloc_error {
    bytes initial = {};
    std.bytes::append(&initial, connection.buffered.as_slice());
    own dyn(std.stream::Stream)* transport =
        match (move connection) { case { .transport = move taken }: move taken; };
    reader_state first_state = {.buffered = move initial, .start = 0usize, .finished = false};
    return websocket {.transport = move transport,
                      .reading = std.async::mutex_new(move first_state),
                      .writing = std.async::mutex_new(writer_state {.closed = false}),
                      .client = client, .max_message = 1048576usize};
}

/* R-SLIB-WS-0003: the largest message receive accepts, 1 MiB unless set. */
void websocket::set_max_message(websocket* this, usize limit) { this->max_message = limit; }

/* Writes one frame under the write lock; nothing is sent after a close frame. */
@scoped
protected async void send_frame(const websocket* this, u8 opcode, const u8[] payload)
    throws websocket_error, std.error::fault {
    bytes frame = encode_frame(opcode, true, payload, this->client);
    std.async::mutex_guard<writer_state> guard = await this->writing.lock();
    throw ((guard.get())->closed == true) failure(error_code::closed);
    if (opcode == 8u8) { (guard.get_mut())->closed = true; }
    task_scope(1) io {
        await this->transport.write_all_from(frame.as_slice());
        await this->transport.flush();
    }
    (move guard).unlock();
}

/* R-SLIB-WS-0005: sends a text message. */
@scoped
async void websocket::send_text(const websocket* this, str text) throws websocket_error, std.error::fault {
    task_scope(1) io { await send_frame(this, 1u8, text); }
}

/* R-SLIB-WS-0005: sends a binary message. */
@scoped
async void websocket::send_binary(const websocket* this, const u8[] data)
    throws websocket_error, std.error::fault {
    task_scope(1) io { await send_frame(this, 2u8, data); }
}

/* R-SLIB-WS-0005: sends a ping of at most 125 bytes; the peer answers with a pong that receive
   skips. */
@scoped
async void websocket::ping(const websocket* this, const u8[] data) throws websocket_error, std.error::fault {
    throw (len(data) > 125usize) failure(error_code::protocol_error);
    task_scope(1) io { await send_frame(this, 9u8, data); }
}

/* R-SLIB-WS-0005: starts the closing handshake with a status code and a reason of at most 123
   bytes; receive then returns the close of the peer. */
@scoped
async void websocket::close(const websocket* this, u16 code, str reason)
    throws websocket_error, std.error::fault {
    const u8[] reason_bytes = reason;
    throw (len(reason_bytes) > 123usize) failure(error_code::protocol_error);
    bytes payload = {};
    std.bytes::append_u8(&payload, ((code as u32) >> 8u32) as u8);
    std.bytes::append_u8(&payload, ((code as u32) & 255u32) as u8);
    std.bytes::append(&payload, reason_bytes);
    task_scope(1) io { await send_frame(this, 8u8, payload.as_slice()); }
}

/* Reads until at least wanted bytes are buffered; false when the stream ends first. */
@scoped
protected async bool fill(const websocket* this, reader_state* state, usize wanted)
    throws std.error::fault {
    u8[4096] chunk = {};
    while (len(state->buffered) - state->start < wanted) {
        usize count = 0usize;
        task_scope(1) io { count += await this->transport.read_into(&chunk); }
        if (count == 0usize) { return false; }
        if (state->start > 0usize) {
            bytes rest = {};
            std.bytes::append(&rest, state->buffered[state->start..len(state->buffered)]);
            bytes old = core::replace(&state->buffered, move rest);
            drop old;
            state->start = 0usize;
        }
        std.bytes::append(&state->buffered, chunk[0usize..count]);
    }
    return true;
}

/* One decoded frame. */
protected struct frame { bool final; u8 opcode; bytes payload; };

/* The length of the payload of the frame header at at, whose second byte gave short. */
protected u64 payload_length(const u8[] data, usize at, u32 short) {
    if (short < 126u32) { return short as u64; }
    if (short == 126u32) { return (((data[at + 2usize] as u32) << 8u32) | (data[at + 3usize] as u32)) as u64; }
    u64 value = 0u64;
    for (usize index = 2usize; index < 10usize; index += 1usize) {
        value = (value << 8u64) | (data[at + index] as u64);
    }
    return value;
}

/* Reads the next frame; none when the stream ends before its first byte. */
@scoped
protected async o<frame> read_frame(const websocket* this, reader_state* state)
    throws websocket_error, std.error::fault {
    task_scope(1) begin {
        bool started = await fill(this, state, 2usize);
        if (started == false) { return o::none; }
    }
    usize at = state->start;
    u32 first = state->buffered[at] as u32;
    u32 second = state->buffered[at + 1usize] as u32;
    u8 opcode = (first & 15u32) as u8;
    bool final = (first & 128u32) != 0u32;
    bool masked = (second & 128u32) != 0u32;
    u32 short = second & 127u32;
    throw ((first & 112u32) != 0u32) failure(error_code::protocol_error);
    throw (opcode != 0u8 && opcode != 1u8 && opcode != 2u8 && opcode != 8u8 && opcode != 9u8 &&
           opcode != 10u8)
        failure(error_code::protocol_error);
    throw (masked == this->client) failure(error_code::protocol_error);
    throw (opcode >= 8u8 && (final == false || short > 125u32)) failure(error_code::protocol_error);
    usize header = 2usize;
    if (short == 126u32) { header = 4usize; }
    if (short == 127u32) { header = 10usize; }
    usize key_at = header;
    if (masked == true) { header += 4usize; }
    task_scope(1) extended {
        bool complete = await fill(this, state, header);
        throw (complete == false) failure(error_code::closed);
    }
    u64 size = payload_length(state->buffered.as_slice(), state->start, short);
    throw (size > (this->max_message as u64)) failure(error_code::message_too_large);
    usize count = size as usize;
    task_scope(1) body {
        bool complete = await fill(this, state, header + count);
        throw (complete == false) failure(error_code::closed);
    }
    usize base = state->start;
    bytes payload = std.bytes::with_capacity(count);
    for (usize index = 0usize; index < count; index += 1usize) {
        u32 value = state->buffered[base + header + index] as u32;
        if (masked == true) { value ^= state->buffered[base + key_at + (index & 3usize)] as u32; }
        std.bytes::append_u8(&payload, value as u8);
    }
    state->start = base + header + count;
    return o::some(frame {.final = final, .opcode = opcode, .payload = move payload});
}

/* Whether a status code may appear in a close frame (RFC 6455 section 7.4). */
protected bool valid_close_code(u16 code) {
    u32 value = code as u32;
    if (value >= 1000u32 && value <= 1003u32) { return true; }
    if (value >= 1007u32 && value <= 1011u32) { return true; }
    return value >= 3000u32 && value <= 4999u32;
}

/* The status code of the close frame that answers a failure. */
protected u16 close_code_of(error_code code) {
    switch (code) {
    case error_code::invalid_data: return 1007u16;
    case error_code::message_too_large: return 1009u16;
    default: return 1002u16;
    }
}

/* Answers a ping with a pong unless a close frame has been sent. */
@scoped
protected async void answer_ping(const websocket* this, const u8[] data) throws std.error::fault {
    try {
        task_scope(1) io { await send_frame(this, 10u8, data); }
    } catch (websocket_error rejected) {
        rejected as void;
    }
}

/* Receives the next message under the read lock. */
@scoped
protected async o<message> receive_locked(const websocket* this, reader_state* state)
    throws websocket_error, std.error::fault {
    bytes assembled = {};
    u8 kind = 0u8;
    while (true) {
        o<frame> next = o::none;
        task_scope(1) io {
            o<frame> got = await read_frame(this, state);
            o<frame> old = core::replace(&next, move got);
            drop old;
        }
        switch (move next) {
        case variant o::none: throw failure(error_code::closed);
        case variant o::some(move item):
            bool final = item.final;
            u8 opcode = item.opcode;
            bytes payload = match (move item) { case { .payload = move taken }: move taken; };
            if (opcode == 9u8) {
                task_scope(1) pong { await answer_ping(this, payload.as_slice()); }
                continue;
            }
            if (opcode == 10u8) {
                drop payload;
                continue;
            }
            if (opcode == 8u8) {
                state->finished = true;
                throw (len(payload) == 1usize) failure(error_code::protocol_error);
                u16 code = 1005u16;
                bytes reason = {};
                if (len(payload) >= 2usize) {
                    code = (((payload[0usize] as u32) << 8u32) | (payload[1usize] as u32)) as u16;
                    throw (valid_close_code(code) == false) failure(error_code::protocol_error);
                    std.bytes::append(&reason, payload[2usize..len(payload)]);
                    try {
                        str checked = core::validate_utf8(reason.as_slice());
                        checked as void;
                    } catch (core::utf8_error rejected) {
                        rejected as void;
                        throw failure(error_code::invalid_data);
                    }
                }
                u8[2] echo = {};
                u32 echoed = 0u32;
                if (code != 1005u16) {
                    echo[0usize] = payload[0usize];
                    echo[1usize] = payload[1usize];
                    echoed = 2u32;
                }
                try {
                    task_scope(1) reply { await send_frame(this, 8u8, echo[0usize..echoed as usize]); }
                } catch (websocket_error rejected) {
                    // The close of this end was sent first; the peer has answered it.
                    rejected as void;
                }
                task_scope(1) end { await this->transport.shutdown(); }
                return o::some(message {.kind = message_kind::close, .data = move reason, .code = code});
            }
            if (opcode == 0u8) {
                throw (kind == 0u8) failure(error_code::protocol_error);
            } else {
                throw (kind != 0u8) failure(error_code::protocol_error);
                kind = opcode;
            }
            throw (len(assembled) + len(payload) > this->max_message) failure(error_code::message_too_large);
            std.bytes::append(&assembled, payload.as_slice());
            if (final == false) { continue; }
            message_kind shape = message_kind::binary;
            if (kind == 1u8) {
                shape = message_kind::text;
                try {
                    str checked = core::validate_utf8(assembled.as_slice());
                    checked as void;
                } catch (core::utf8_error rejected) {
                    rejected as void;
                    throw failure(error_code::invalid_data);
                }
            }
            return o::some(message {.kind = shape, .data = move assembled, .code = 0u16});
        }
    }
    throw failure(error_code::closed);
}

/* R-SLIB-WS-0005: the next text or binary message, whose fragments are joined, or the close of
   the peer, which is answered; none after the close. Pings are answered and pongs skipped. A
   frame that breaks RFC 6455, text that is not UTF-8 and a message above the limit are answered
   with a close of status 1002, 1007 or 1009 and throw; a stream that ends without a close
   throws closed. */
@scoped
async o<message> websocket::receive(const websocket* this) throws websocket_error, std.error::fault {
    std.async::mutex_guard<reader_state> guard = await this->reading.lock();
    reader_state* state = guard.get_mut();
    if (state->finished == true) { return o::none; }
    try {
        task_scope(1) io { return await receive_locked(this, state); }
    } catch (websocket_error rejected) {
        state->finished = true;
        if (rejected.code != error_code::closed) {
            bytes payload = {};
            u32 status = close_code_of(rejected.code) as u32;
            std.bytes::append_u8(&payload, (status >> 8u32) as u8);
            std.bytes::append_u8(&payload, (status & 255u32) as u8);
            try {
                task_scope(1) reply { await send_frame(this, 8u8, payload.as_slice()); }
            } catch (websocket_error ignored) {
                ignored as void;
            } catch (std.error::fault ignored) {
                ignored as void;
            }
        }
        throw rejected;
    }
    return o::none;
}

protected void put(std.http::headers* fields, str name, str value) throws std.alloc::alloc_error {
    try {
        fields->add(name, value);
    } catch (std.http::http_error rejected) {
        // The names and values of the handshake are valid.
        rejected as void;
    }
}

/* Whether the answer to a handshake accepts it: 101 with Upgrade websocket, Connection Upgrade,
   the expected accept value and the subprotocol asked for, if any. */
protected bool switched_to(const std.http::response* answer, str expected, str protocol) {
    const std.http::headers* fields = &answer->headers;
    if (answer->status != 101u16 || fields->has_token("Upgrade", "websocket") == false ||
        fields->has_token("Connection", "upgrade") == false) {
        return false;
    }
    switch (fields->get("Sec-WebSocket-Accept")) {
    case variant o::some(value):
        if (std.bytes::equal(std.text::trim(*value), expected) == false) { return false; }
    case variant o::none: return false;
    }
    const u8[] protocol_bytes = protocol;
    switch (fields->get("Sec-WebSocket-Protocol")) {
    case variant o::some(value):
        return len(protocol_bytes) != 0usize && std.bytes::equal(std.text::trim(*value), protocol) == true;
    case variant o::none: return len(protocol_bytes) == 0usize;
    }
}

/* R-SLIB-WS-0002: opens a WebSocket to a ws or wss URL (http and https are also accepted) with
   the client: a GET with a random key, version 13 and, unless protocol is empty, the
   subprotocol. The answer shall be 101 with Upgrade websocket, Connection Upgrade, the accept
   value of the key and the same subprotocol. */
@scoped
async websocket connect(std.http::client* web, str address, str protocol)
    throws websocket_error, std.http::http_error, std.tls::tls_error, std.error::fault {
    std.string::string target = std.string::create();
    const u8[] address_bytes = address;
    if (std.text::starts_with(address, "ws://") == true) {
        std.string::append_str(&target, "http://");
        std.string::append_utf8(&target, address_bytes[5usize..len(address_bytes)]);
    } else {
        if (std.text::starts_with(address, "wss://") == true) {
            std.string::append_str(&target, "https://");
            std.string::append_utf8(&target, address_bytes[6usize..len(address_bytes)]);
        } else {
            std.string::append_str(&target, address);
        }
    }
    u8[16] nonce = {};
    std.random::fill(&nonce);
    std.string::string key = std.encoding::encode_base64(&nonce);
    std.string::string expected = accept_key(key);
    const u8[] protocol_bytes = protocol;
    bool wants_protocol = len(protocol_bytes) != 0usize;
    std.http::request asking = std.http::request::create(std.http::method::get, "/");
    put(&asking.headers, "Upgrade", "websocket");
    put(&asking.headers, "Connection", "Upgrade");
    put(&asking.headers, "Sec-WebSocket-Key", key);
    put(&asking.headers, "Sec-WebSocket-Version", "13");
    if (wants_protocol == true) { put(&asking.headers, "Sec-WebSocket-Protocol", protocol); }
    o<std.http::upgraded> connection = o::none;
    task_scope(1) io {
        std.http::handshake answered = await web->upgrade(move asking, target);
        bool accepted = switched_to(&answered.answer, expected, protocol);
        throw (accepted == false) failure(error_code::handshake_failed);
        o<std.http::upgraded> taken = core::replace(&answered.connection, o::none);
        o<std.http::upgraded> old = core::replace(&connection, move taken);
        drop old;
    }
    switch (move connection) {
    case variant o::some(move switched): return websocket::create(move switched, true);
    case variant o::none: throw failure(error_code::handshake_failed);
    }
}

/* Whether the comma-separated list of the field names the token, compared exactly. */
protected bool lists_token(const std.http::headers* fields, str name, str token) {
    switch (fields->get(name)) {
    case variant o::some(value):
        for (str item in std.text::split(*value, ",")) {
            if (std.bytes::equal(std.text::trim(item), token) == true) { return true; }
        }
        return false;
    case variant o::none: return false;
    }
}

/* R-SLIB-WS-0002: answers the request of an upgrade route with a WebSocket. The request shall
   be a GET with Upgrade websocket, Connection Upgrade, version 13 and a key of 16 bytes in
   base64, and, unless protocol is empty, offer the subprotocol, which the answer then names;
   otherwise the upgrade is refused with 400, or 426 and the supported version, and
   handshake_failed is thrown. */
@scoped
async websocket accept(std.http::request incoming, std.http::upgrade connection, str protocol)
    throws websocket_error, std.error::fault {
    const std.http::headers* fields = &incoming.headers;
    bool valid = incoming.method == std.http::method::get &&
                 fields->has_token("Upgrade", "websocket") == true &&
                 fields->has_token("Connection", "upgrade") == true;
    bool version = false;
    switch (fields->get("Sec-WebSocket-Version")) {
    case variant o::some(value): version = std.bytes::equal(std.text::trim(*value), "13");
    case variant o::none: break;
    }
    std.string::string key = std.string::create();
    switch (fields->get("Sec-WebSocket-Key")) {
    case variant o::some(value):
        str trimmed = std.text::trim(*value);
        try {
            bytes decoded = std.encoding::decode_base64(trimmed);
            if (len(decoded) == 16usize) { std.string::append_str(&key, trimmed); }
        } catch (std.convert::parse_error rejected) {
            rejected as void;
        }
    case variant o::none: break;
    }
    if (std.string::len(&key) == 0usize) { valid = false; }
    const u8[] protocol_bytes = protocol;
    bool wants_protocol = len(protocol_bytes) != 0usize;
    if (wants_protocol == true && lists_token(fields, "Sec-WebSocket-Protocol", protocol) == false) {
        valid = false;
    }
    if (valid == false || version == false) {
        u16 status = 400u16;
        if (valid == true) { status = 426u16; }
        std.http::response refusal = std.http::response::text(status, std.http::reason(status));
        put(&refusal.headers, "Sec-WebSocket-Version", "13");
        drop incoming;
        task_scope(1) refuse { await (move connection).refuse(move refusal); }
        throw failure(error_code::handshake_failed);
    }
    drop incoming;
    std.string::string answer = accept_key(key);
    std.http::headers reply = std.http::headers::create();
    put(&reply, "Upgrade", "websocket");
    put(&reply, "Connection", "Upgrade");
    put(&reply, "Sec-WebSocket-Accept", answer);
    if (wants_protocol == true) { put(&reply, "Sec-WebSocket-Protocol", protocol); }
    std.http::upgraded switched = await (move connection).accept(move reply);
    return websocket::create(move switched, false);
}
