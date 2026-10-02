module std.dns;
import std.net;
import std.text;
import std.cmp;
import std.slice;

/* R-SLIB-DNS-0001: why a name could not be looked up. */
@derive(format)
enum error_code {
    invalid_name,
    name_error,
    server_failure,
    refused,
    format_error,
    timed_out,
    malformed_response,
    no_servers,
};

/* R-SLIB-DNS-0001: a failure of a lookup. */
error dns_error { error_code code; };

protected dns_error failure(error_code code) { return dns_error {.code = code}; }

/* R-SLIB-DNS-0002: the record types that the module decodes. */
@derive(format)
enum record_type { a, ns, cname, ptr, mx, txt, aaaa, srv };

/* R-SLIB-DNS-0002: the numeric TYPE of a record type (RFC 1035, 2782, 3596). */
u16 type_code(record_type kind) {
    switch (kind) {
    case record_type::a: return 1u16;
    case record_type::ns: return 2u16;
    case record_type::cname: return 5u16;
    case record_type::ptr: return 12u16;
    case record_type::mx: return 15u16;
    case record_type::txt: return 16u16;
    case record_type::aaaa: return 28u16;
    default: return 33u16;
    }
}

protected const u16 class_in = 1u16;
protected const u16 type_opt = 41u16;

/* The types whose data is one domain name, or whose data ends with one. */
protected bool has_name_data(u16 kind) {
    return kind == 2u16 || kind == 5u16 || kind == 12u16 || kind == 15u16 || kind == 33u16;
}

@generic<T>
protected void append(array<T>* target, T value) throws std.alloc::alloc_error {
    try {
        target->push(move value);
    } catch (std.array::push_error<T> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected void put_u16(bytes* out, u16 value) throws std.alloc::alloc_error {
    u32 number = value as u32;
    std.bytes::append_u8(out, (number >> 8u32) as u8);
    std.bytes::append_u8(out, (number & 255u32) as u8);
}

protected void put_u32(bytes* out, u32 value) throws std.alloc::alloc_error {
    put_u16(out, (value >> 16u32) as u16);
    put_u16(out, (value & 65535u32) as u16);
}

/* R-SLIB-DNS-0003: appends a domain name in the uncompressed wire form: labels of 1 to 63 bytes
   separated by dots, an optional final dot, at most 255 bytes on the wire; "" and "." are the
   root. */
protected void put_name(bytes* out, str name) throws dns_error, std.alloc::alloc_error {
    const u8[] bytes_of = name;
    usize end = len(bytes_of);
    if (end > 0usize && bytes_of[end - 1usize] == 46u8) { end -= 1usize; }
    throw (end + 2usize > 255usize) failure(error_code::invalid_name);
    usize start = 0usize;
    while (start < end) {
        usize stop = start;
        while (stop < end && bytes_of[stop] != 46u8) { stop += 1usize; }
        usize size = stop - start;
        throw (size == 0usize || size > 63usize) failure(error_code::invalid_name);
        std.bytes::append_u8(out, size as u8);
        std.bytes::append(out, bytes_of[start..stop]);
        start = stop + 1usize;
    }
    std.bytes::append_u8(out, 0u8);
}

/* R-SLIB-DNS-0003: one record of an answer: its owner name, type, TTL and data in the
   uncompressed wire form, with the decoded fields of the types that carry them. */
struct record {
    std.string::string name;
    u16 kind;
    u32 ttl;
    bytes data;
    std.string::string target;
    u16 priority;
    u16 weight;
    u16 port;
};

protected record empty_record(str name, u16 kind, u32 ttl) throws std.alloc::alloc_error {
    return record {.name = std.string::from_str(name), .kind = kind, .ttl = ttl, .data = {},
                   .target = std.string::create(), .priority = 0u16, .weight = 0u16,
                   .port = 0u16};
}

/* R-SLIB-DNS-0003: an SRV record (RFC 2782). */
record record::srv(str name, u32 ttl, u16 priority, u16 weight, u16 port, str target)
    throws dns_error, std.alloc::alloc_error {
    record result = empty_record(name, 33u16, ttl);
    put_u16(&result.data, priority);
    put_u16(&result.data, weight);
    put_u16(&result.data, port);
    put_name(&result.data, target);
    std.string::append_str(&result.target, target);
    result.priority = priority;
    result.weight = weight;
    result.port = port;
    return move result;
}

/* R-SLIB-DNS-0003: a PTR record. */
record record::ptr(str name, u32 ttl, str target) throws dns_error, std.alloc::alloc_error {
    record result = empty_record(name, 12u16, ttl);
    put_name(&result.data, target);
    std.string::append_str(&result.target, target);
    return move result;
}

/* R-SLIB-DNS-0003: a TXT record of one character string, split into strings of at most 255
   bytes. */
record record::txt(str name, u32 ttl, str text) throws std.alloc::alloc_error {
    record result = empty_record(name, 16u16, ttl);
    const u8[] bytes_of = text;
    usize start = 0usize;
    while (start < len(bytes_of) || start == 0usize) {
        usize stop = start + 255usize;
        if (stop > len(bytes_of)) { stop = len(bytes_of); }
        std.bytes::append_u8(&result.data, (stop - start) as u8);
        std.bytes::append(&result.data, bytes_of[start..stop]);
        if (stop == len(bytes_of)) { break; }
        start = stop;
    }
    return move result;
}

/* R-SLIB-DNS-0003: an A or AAAA record of an address. */
record record::address(str name, u32 ttl, std.net::ip_address value) throws std.alloc::alloc_error {
    bytes octets = std.net::ip_octets(value);
    u16 kind = 1u16;
    if (len(octets) == 16usize) { kind = 28u16; }
    record result = empty_record(name, kind, ttl);
    std.bytes::append(&result.data, octets.as_slice());
    return move result;
}

/* R-SLIB-DNS-0003: the question of a query. */
struct question { u16 id; std.string::string name; u16 kind; bool recursion; };

protected u16 read_u16(const u8[] message, usize at) throws dns_error {
    throw (at + 2usize > len(message)) failure(error_code::malformed_response);
    return (((message[at] as u32) << 8u32) | (message[at + 1usize] as u32)) as u16;
}

protected u32 read_u32(const u8[] message, usize at) throws dns_error {
    u32 high = read_u16(message, at) as u32;
    u32 low = read_u16(message, at + 2usize) as u32;
    return (high << 16u32) | low;
}

/* Reads the name at *at, following compression pointers backwards at most 128 times, appends
   it to out in dotted form without a final dot and moves *at past its first occurrence. */
protected void read_name(const u8[] message, usize* at, std.string::string* out)
    throws dns_error, std.alloc::alloc_error {
    usize position = *at;
    bool jumped = false;
    u32 jumps = 0u32;
    usize total = 0usize;
    while (true) {
        throw (position >= len(message)) failure(error_code::malformed_response);
        u32 size = message[position] as u32;
        if (size == 0u32) {
            if (jumped == false) { *at = position + 1usize; }
            return;
        }
        if ((size & 192u32) == 192u32) {
            throw (position + 1usize >= len(message)) failure(error_code::malformed_response);
            usize target = (((size & 63u32) << 8u32) | (message[position + 1usize] as u32)) as usize;
            throw (target >= position) failure(error_code::malformed_response);
            jumps += 1u32;
            throw (jumps > 128u32) failure(error_code::malformed_response);
            if (jumped == false) { *at = position + 2usize; }
            jumped = true;
            position = target;
            continue;
        }
        throw ((size & 192u32) != 0u32) failure(error_code::malformed_response);
        usize count = size as usize;
        throw (position + 1usize + count > len(message)) failure(error_code::malformed_response);
        total += count + 1usize;
        throw (total > 255usize) failure(error_code::malformed_response);
        if (std.string::len(out) != 0usize) { std.string::append_str(out, "."); }
        try {
            std.string::append_utf8(out, message[position + 1usize..position + 1usize + count]);
        } catch (std.string::string_error rejected) {
            rejected as void;
            throw failure(error_code::malformed_response);
        }
        position += 1usize + count;
    }
}

/* R-SLIB-DNS-0003: the question of a query message: its id, name and type; the message shall
   be a query (QR clear, opcode 0) with exactly one question of class IN. */
question parse_query(const u8[] message) throws dns_error, std.alloc::alloc_error {
    throw (len(message) < 12usize) failure(error_code::format_error);
    u32 flags = read_u16(message, 2usize) as u32;
    throw ((flags & 32768u32) != 0u32 || (flags & 30720u32) != 0u32) failure(error_code::format_error);
    throw (read_u16(message, 4usize) != 1u16) failure(error_code::format_error);
    usize at = 12usize;
    std.string::string name = std.string::create();
    try {
        read_name(message, &at, &name);
    } catch (dns_error rejected) {
        rejected as void;
        throw failure(error_code::format_error);
    }
    u16 kind = read_u16(message, at);
    throw (read_u16(message, at + 2usize) != class_in) failure(error_code::format_error);
    return question {.id = read_u16(message, 0usize), .name = move name, .kind = kind,
                     .recursion = (flags & 256u32) != 0u32};
}

/* R-SLIB-DNS-0003: a query message: the id, recursion desired, one question of class IN and an
   EDNS0 OPT record that accepts responses of 1232 bytes over UDP. */
bytes encode_query(u16 id, str name, u16 kind) throws dns_error, std.alloc::alloc_error {
    bytes out = std.bytes::with_capacity(64usize);
    put_u16(&out, id);
    put_u16(&out, 256u16);
    put_u16(&out, 1u16);
    put_u16(&out, 0u16);
    put_u16(&out, 0u16);
    put_u16(&out, 1u16);
    put_name(&out, name);
    put_u16(&out, kind);
    put_u16(&out, class_in);
    std.bytes::append_u8(&out, 0u8);
    put_u16(&out, type_opt);
    put_u16(&out, 1232u16);
    put_u32(&out, 0u32);
    put_u16(&out, 0u16);
    return move out;
}

/* R-SLIB-DNS-0003: a response to a question with the records of its answer section, the
   response code (0 success, 1 format error, 2 server failure, 3 name error, 5 refused) and the
   TC flag; names are not compressed. */
bytes encode_answer(const question* asked, const array<record>* answers, u8 rcode, bool truncated)
    throws dns_error, std.alloc::alloc_error {
    bytes out = std.bytes::with_capacity(128usize);
    u32 flags = 32768u32 | 1024u32 | 128u32 | ((rcode as u32) & 15u32);
    if (asked->recursion == true) { flags |= 256u32; }
    if (truncated == true) { flags |= 512u32; }
    put_u16(&out, asked->id);
    put_u16(&out, flags as u16);
    put_u16(&out, 1u16);
    put_u16(&out, len(*answers) as u16);
    put_u16(&out, 0u16);
    put_u16(&out, 0u16);
    put_name(&out, asked->name.as_str());
    put_u16(&out, asked->kind);
    put_u16(&out, class_in);
    for (usize index = 0usize; index < len(*answers); index += 1usize) {
        put_name(&out, (*answers)[index].name.as_str());
        put_u16(&out, (*answers)[index].kind);
        put_u16(&out, class_in);
        put_u32(&out, (*answers)[index].ttl);
        put_u16(&out, len((*answers)[index].data) as u16);
        std.bytes::append(&out, (*answers)[index].data.as_slice());
    }
    return move out;
}

/* The name without one final dot. */
protected str without_final_dot(str name) {
    const u8[] bytes_of = name;
    usize end = len(bytes_of);
    if (end > 0usize && bytes_of[end - 1usize] == 46u8) { end -= 1usize; }
    try {
        return core::validate_utf8(bytes_of[0usize..end]);
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    return name;
}

/* Whether a response has its TC flag: the answer did not fit a UDP datagram. */
protected bool truncated_response(const u8[] message) {
    if (len(message) < 4usize) { return false; }
    return ((message[2usize] as u32) & 2u32) != 0u32;
}

/* Decodes the data of a record, rewriting the names in it to their uncompressed form. */
protected void decode_data(const u8[] message, usize start, usize size, record* entry)
    throws dns_error, std.alloc::alloc_error {
    usize end = start + size;
    usize at = start;
    if (entry->kind == 33u16) {
        throw (size < 7usize) failure(error_code::malformed_response);
        entry->priority = read_u16(message, at);
        entry->weight = read_u16(message, at + 2usize);
        entry->port = read_u16(message, at + 4usize);
        at += 6usize;
    }
    if (entry->kind == 15u16) {
        throw (size < 3usize) failure(error_code::malformed_response);
        entry->priority = read_u16(message, at);
        at += 2usize;
    }
    if (has_name_data(entry->kind) == true) {
        read_name(message, &at, &entry->target);
        throw (at != end) failure(error_code::malformed_response);
        if (entry->kind == 33u16) {
            put_u16(&entry->data, entry->priority);
            put_u16(&entry->data, entry->weight);
            put_u16(&entry->data, entry->port);
        }
        if (entry->kind == 15u16) { put_u16(&entry->data, entry->priority); }
        put_name(&entry->data, entry->target.as_str());
        return;
    }
    std.bytes::append(&entry->data, message[start..end]);
}

/* R-SLIB-DNS-0003: the records of type kind and class IN in the answer section of a response to
   the query with id for name, in order. A response that is not the answer to that question is
   malformed_response; response codes 1, 2, 3 and 5 are format_error, server_failure, name_error
   and refused, and others server_failure. */
array<record> parse_response(const u8[] message, u16 id, str name, u16 kind)
    throws dns_error, std.alloc::alloc_error {
    throw (len(message) < 12usize) failure(error_code::malformed_response);
    u32 flags = read_u16(message, 2usize) as u32;
    throw (read_u16(message, 0usize) != id || (flags & 32768u32) == 0u32 || (flags & 30720u32) != 0u32)
        failure(error_code::malformed_response);
    u32 rcode = flags & 15u32;
    throw (rcode == 1u32) failure(error_code::format_error);
    throw (rcode == 3u32) failure(error_code::name_error);
    throw (rcode == 5u32) failure(error_code::refused);
    throw (rcode != 0u32) failure(error_code::server_failure);
    u16 questions = read_u16(message, 4usize);
    u16 answers = read_u16(message, 6usize);
    throw (questions != 1u16) failure(error_code::malformed_response);
    usize at = 12usize;
    std.string::string asked = std.string::create();
    read_name(message, &at, &asked);
    str wanted_name = without_final_dot(name);
    throw (std.text::equal_ignore_ascii_case(asked.as_str(), wanted_name) == false ||
           read_u16(message, at) != kind || read_u16(message, at + 2usize) != class_in)
        failure(error_code::malformed_response);
    at += 4usize;
    array<record> result = std.array::create::<record>();
    for (u16 index = 0u16; index < answers; index += 1u16) {
        std.string::string owner = std.string::create();
        read_name(message, &at, &owner);
        u16 record_kind = read_u16(message, at);
        bool matching = record_kind == kind && read_u16(message, at + 2usize) == class_in;
        usize fields = at;
        usize size = read_u16(message, at + 8usize) as usize;
        at += 10usize;
        throw (at + size > len(message)) failure(error_code::malformed_response);
        if (matching == true) {
            record entry = empty_record(owner.as_str(), kind, read_u32(message, fields + 4usize));
            decode_data(message, at, size, &entry);
            append(&result, move entry);
        }
        at += size;
    }
    return move result;
}

/* R-SLIB-DNS-0004: the name servers a resolver asks, in order, the time it waits for each
   answer and how many times it asks the whole list. */
struct resolver {
    protected array<std.net::socket_address> servers;
    protected std.time::duration timeout;
    protected u32 attempts;
};

resolver resolver::with_servers(array<std.net::socket_address> servers) {
    return resolver {.servers = move servers, .timeout = std.time::duration_from_seconds(2i64),
                     .attempts = 2u32};
}

void resolver::set_timeout(resolver* this, std.time::duration timeout) { this->timeout = timeout; }

void resolver::set_attempts(resolver* this, u32 attempts) {
    if (attempts == 0u32) {
        this->attempts = 1u32;
        return;
    }
    this->attempts = attempts;
}

/* The part of text from byte start on. */
protected str without_final_dot_piece(str text, usize start) {
    const u8[] bytes_of = text;
    try {
        return core::validate_utf8(bytes_of[start..len(bytes_of)]);
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    return "";
}

/* The servers of the "nameserver" lines of a resolv.conf text, port 53; a scope suffix after
   '%' is dropped and lines that are not addresses are skipped. */
array<std.net::socket_address> parse_resolv_conf(str text) throws std.alloc::alloc_error {
    array<std.net::socket_address> servers = std.array::create::<std.net::socket_address>();
    for (str text_line in std.text::lines(text)) {
        str line = std.text::trim(text_line);
        if (std.text::starts_with(line, "nameserver") == false) { continue; }
        const u8[] bytes_of = line;
        usize start = 10usize;
        if (start >= len(bytes_of) || (bytes_of[start] != 32u8 && bytes_of[start] != 9u8)) { continue; }
        str rest = std.text::trim(without_final_dot_piece(line, start));
        const u8[] address_bytes = rest;
        usize end = 0usize;
        while (end < len(address_bytes) && address_bytes[end] != 37u8 && address_bytes[end] != 32u8 &&
               address_bytes[end] != 9u8) {
            end += 1usize;
        }
        try {
            str address_text = core::validate_utf8(address_bytes[0usize..end]);
            std.net::ip_address address = std.net::parse_ip(address_text);
            append(&servers, std.net::socket_address {.address = address, .port = 53u16, .scope_id = 0u32});
        } catch (std.net::address_error rejected) {
            rejected as void;
        } catch (core::utf8_error rejected) {
            rejected as void;
        }
    }
    return move servers;
}

/* R-SLIB-DNS-0004: a resolver of the name servers of /etc/resolv.conf; none is no_servers. */
async resolver resolver::system() throws dns_error, std.error::fault {
    std.fs::path path = std.fs::path_from_utf8("/etc/resolv.conf");
    bytes content = await path.read_file(65536usize);
    std.string::string text = std.string::create();
    try {
        std.string::append_utf8(&text, content.as_slice());
    } catch (std.string::string_error rejected) {
        rejected as void;
        throw failure(error_code::no_servers);
    }
    array<std.net::socket_address> servers = parse_resolv_conf(text.as_str());
    throw (len(servers) == 0usize) failure(error_code::no_servers);
    return resolver::with_servers(move servers);
}

protected u16 random_id() {
    u8[2] random = {};
    std.random::fill(&random);
    return (((random[0usize] as u32) << 8u32) | (random[1usize] as u32)) as u16;
}

/* Whether two socket addresses are the same address and port. */
protected bool same_endpoint(std.net::socket_address left, std.net::socket_address right)
    throws std.alloc::alloc_error {
    if (left.port != right.port) { return false; }
    bytes first = std.net::ip_octets(left.address);
    bytes second = std.net::ip_octets(right.address);
    return std.bytes::equal(first.as_slice(), second.as_slice());
}

/* Sends the query to the server over UDP and returns the first datagram from the server that
   carries the id; others are ignored. */
@scoped
protected async bytes exchange_udp(std.net::socket_address server, const u8[] query, u16 id)
    throws std.error::fault {
    bytes octets = std.net::ip_octets(server.address);
    str any = "0.0.0.0";
    if (len(octets) == 16usize) { any = "::"; }
    std.net::socket_address local = {.address = std.net::parse_ip(any), .port = 0u16, .scope_id = 0u32};
    std.net::udp_socket socket = await local.bind(false);
    u8[4096] buffer = {};
    bytes reply = {};
    task_scope(1) io {
        await socket.send_from(server, query);
        bool waiting = true;
        while (waiting == true) {
            std.net::datagram arrival = await std.net::udp_receive_into(&socket, &buffer);
            if (arrival.truncated == false && arrival.count >= 12usize &&
                same_endpoint(arrival.peer, server) == true &&
                ((((buffer[0usize] as u32) << 8u32) | (buffer[1usize] as u32)) as u16) == id) {
                std.bytes::append(&reply, buffer[0usize..arrival.count]);
                waiting = false;
            }
        }
    }
    await (move socket).close();
    return move reply;
}

/* Sends the query to the server over TCP with its two-byte length and returns the answer. */
@scoped
protected async bytes exchange_tcp(std.net::socket_address server, const u8[] query)
    throws dns_error, std.error::fault {
    std.net::tcp_stream stream = await server.connect();
    bytes framed = {};
    std.bytes::append_u8(&framed, ((len(query) >> 8usize) & 255usize) as u8);
    std.bytes::append_u8(&framed, (len(query) & 255usize) as u8);
    std.bytes::append(&framed, query);
    u8[2] prefix = {};
    u8[4096] chunk = {};
    bytes reply = {};
    task_scope(1) io {
        await std.net::tcp_write_all_from(&stream, framed.as_slice());
        usize have = 0usize;
        while (have < 2usize) {
            usize count = await std.net::tcp_read_into(&stream, prefix[have..2usize]);
            throw (count == 0usize) failure(error_code::malformed_response);
            have += count;
        }
        usize size = (((prefix[0usize] as u32) << 8u32) | (prefix[1usize] as u32)) as usize;
        while (len(reply) < size) {
            usize wanted = size - len(reply);
            if (wanted > 4096usize) { wanted = 4096usize; }
            usize count = await std.net::tcp_read_into(&stream, chunk[0usize..wanted]);
            throw (count == 0usize) failure(error_code::malformed_response);
            std.bytes::append(&reply, chunk[0usize..count]);
        }
    }
    await (move stream).close();
    return move reply;
}

/* R-SLIB-DNS-0004: the records of type kind for name. Each attempt asks the servers in order
   over UDP, each within the timeout, and asks again over TCP when an answer is truncated. A
   name error ends the lookup; format errors, server failures, refusals, malformed answers,
   network failures and timeouts move on to the next server, and the lookup reports the last of
   them. The deadline of an enclosing deadline block bounds the whole lookup. */
@scoped
async array<record> resolver::lookup(const resolver* this, str name, u16 kind)
    throws dns_error, std.error::fault {
    throw (len(this->servers) == 0usize) failure(error_code::no_servers);
    bytes checked = encode_query(0u16, name, kind);
    drop checked;
    error_code last = error_code::timed_out;
    for (u32 attempt = 0u32; attempt < this->attempts; attempt += 1u32) {
        for (usize index = 0usize; index < len(this->servers); index += 1usize) {
            std.net::socket_address server = this->servers[index];
            try {
                std.time::instant now = std.time::monotonic_now();
                std.time::instant limit = now.add(this->timeout);
                u16 id = random_id();
                bytes query = encode_query(id, name, kind);
                task_scope(1) io {
                    bytes reply = {};
                    deadline (limit) {
                        bytes received = await exchange_udp(server, query.as_slice(), id);
                        std.bytes::append(&reply, received.as_slice());
                        if (truncated_response(reply.as_slice()) == true) {
                            bytes whole = await exchange_tcp(server, query.as_slice());
                            bytes old = core::replace(&reply, move whole);
                            drop old;
                        }
                    }
                    return parse_response(reply.as_slice(), id, name, kind);
                }
            } catch (dns_error rejected) {
                throw (rejected.code == error_code::name_error) rejected;
                last = rejected.code;
            } catch (std.net::net_error rejected) {
                throw (rejected.code == std.net::error_code::cancelled) rejected;
                if (rejected.code == std.net::error_code::timed_out) { last = error_code::timed_out; }
                if (rejected.code != std.net::error_code::timed_out) { last = error_code::server_failure; }
            } catch (std.time::time_error rejected) {
                rejected as void;
                last = error_code::timed_out;
            }
        }
    }
    throw failure(last);
}

/* R-SLIB-DNS-0005: a service location (RFC 2782). */
struct srv_record { u16 priority; u16 weight; u16 port; std.string::string target; };

protected std.cmp::ordering srv_order(const srv_record* left, const srv_record* right) {
    if (left->priority != right->priority) {
        if (left->priority < right->priority) { return std.cmp::ordering::less; }
        return std.cmp::ordering::greater;
    }
    if (left->weight != right->weight) {
        if (left->weight > right->weight) { return std.cmp::ordering::less; }
        return std.cmp::ordering::greater;
    }
    i32 compared = std.bytes::compare(left->target.as_bytes(), right->target.as_bytes());
    if (compared < 0) { return std.cmp::ordering::less; }
    if (compared > 0) { return std.cmp::ordering::greater; }
    return std.cmp::ordering::equal;
}

/* R-SLIB-DNS-0005: the SRV records of a name such as _service._tcp.example.org, in the order
   of priority, then of weight from the largest, then of target; a single record with the root
   target means that the service is not offered and yields none. */
@scoped
async array<srv_record> resolver::lookup_srv(const resolver* this, str name)
    throws dns_error, std.error::fault {
    array<record> found = std.array::create::<record>();
    task_scope(1) io {
        array<record> answers = await this->lookup(name, 33u16);
        array<record> old = core::replace(&found, move answers);
        drop old;
    }
    array<srv_record> result = std.array::create::<srv_record>();
    for (usize index = 0usize; index < len(found); index += 1usize) {
        std.string::string target = std.string::from_str(found[index].target.as_str());
        append(&result, srv_record {.priority = found[index].priority, .weight = found[index].weight,
                                    .port = found[index].port, .target = move target});
    }
    if (len(result) == 1usize && std.string::len(&result[0usize].target) == 0usize) {
        array<srv_record> none = std.array::create::<srv_record>();
        return move none;
    }
    srv_record[] view = std.array::as_slice_mut(&result);
    fn(const srv_record*, const srv_record*) -> std.cmp::ordering compare = srv_order;
    std.slice::sort_by(view, &compare);
    return move result;
}

/* R-SLIB-DNS-0005: the character strings of one TXT record. */
struct txt_record { array<bytes> strings; };

/* R-SLIB-DNS-0005: the strings of the record joined, as UTF-8 text. */
std.string::string txt_record::text(const txt_record* this) throws dns_error, std.alloc::alloc_error {
    std.string::string result = std.string::create();
    for (usize index = 0usize; index < len(this->strings); index += 1usize) {
        try {
            std.string::append_utf8(&result, this->strings[index].as_slice());
        } catch (std.string::string_error rejected) {
            rejected as void;
            throw failure(error_code::malformed_response);
        }
    }
    return move result;
}

/* R-SLIB-DNS-0005: the TXT records of a name, in the order of the answer. */
@scoped
async array<txt_record> resolver::lookup_txt(const resolver* this, str name)
    throws dns_error, std.error::fault {
    array<record> found = std.array::create::<record>();
    task_scope(1) io {
        array<record> answers = await this->lookup(name, 16u16);
        array<record> old = core::replace(&found, move answers);
        drop old;
    }
    array<txt_record> result = std.array::create::<txt_record>();
    for (usize index = 0usize; index < len(found); index += 1usize) {
        const u8[] data = found[index].data.as_slice();
        array<bytes> strings = std.array::create::<bytes>();
        usize at = 0usize;
        while (at < len(data)) {
            usize size = data[at] as usize;
            throw (at + 1usize + size > len(data)) failure(error_code::malformed_response);
            bytes piece = {};
            std.bytes::append(&piece, data[at + 1usize..at + 1usize + size]);
            append(&strings, move piece);
            at += 1usize + size;
        }
        append(&result, txt_record {.strings = move strings});
    }
    return move result;
}

/* The lower-case hexadecimal digit of a value below 16. */
protected char nibble_char(u32 value) {
    if (value < 10u32) { return (value + 48u32) as u8 as char; }
    return (value + 87u32) as u8 as char;
}

/* R-SLIB-DNS-0006: the name of the reverse lookup of an address: its octets in reverse under
   in-addr.arpa, or its nibbles in reverse under ip6.arpa. */
std.string::string reverse_name(std.net::ip_address address) throws std.alloc::alloc_error {
    bytes octets = std.net::ip_octets(address);
    std.string::string name = std.string::create();
    usize count = len(octets);
    if (count == 4usize) {
        for (usize index = 0usize; index < 4usize; index += 1usize) {
            u8 value = octets[3usize - index];
            std.string::string part = f"{value}.";
            std.string::append_str(&name, part.as_str());
        }
        std.string::append_str(&name, "in-addr.arpa");
        return move name;
    }
    for (usize index = 0usize; index < count; index += 1usize) {
        u32 value = octets[count - 1usize - index] as u32;
        u32 low = value & 15u32;
        u32 high = value >> 4u32;
        std.string::push_scalar(&name, nibble_char(low));
        std.string::append_str(&name, ".");
        std.string::push_scalar(&name, nibble_char(high));
        std.string::append_str(&name, ".");
    }
    std.string::append_str(&name, "ip6.arpa");
    return move name;
}

/* R-SLIB-DNS-0006: the names of an address by its PTR records. */
@scoped
async array<std.string::string> resolver::reverse(const resolver* this, std.net::ip_address address)
    throws dns_error, std.error::fault {
    std.string::string name = reverse_name(address);
    array<record> found = std.array::create::<record>();
    task_scope(1) io {
        array<record> answers = await this->lookup(name.as_str(), 12u16);
        array<record> old = core::replace(&found, move answers);
        drop old;
    }
    array<std.string::string> result = std.array::create::<std.string::string>();
    for (usize index = 0usize; index < len(found); index += 1usize) {
        append(&result, std.string::from_str(found[index].target.as_str()));
    }
    return move result;
}
