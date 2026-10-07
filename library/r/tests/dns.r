module tests.std.dns;
import std.test;
import std.net;
import std.text;
import std.time;
import std.dns;

// The tests of std.dns (Library R-SLIB-DNS-0001..0006): the wire form of queries and answers,
// name compression and its limits, resolv.conf, reverse names, and lookups against a test name
// server on the loopback interface that answers over UDP and, for truncated answers, over TCP
// on the same port. Run in test mode (Core R-FUNC-0025).

protected std.net::socket_address loopback(u16 port) throws std.net::address_error {
    return std.net::socket_address {.address = std.net::parse_ip("127.0.0.1"), .port = port,
                                    .scope_id = 0u32};
}

protected void add(array<std.dns::record>* answers, std.dns::record entry) throws std.alloc::alloc_error {
    try {
        answers->push(move entry);
    } catch (std.array::push_error<std.dns::record> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

protected array<std.net::socket_address> one(std.net::socket_address server) throws std.alloc::alloc_error {
    array<std.net::socket_address> servers = std.array::create::<std.net::socket_address>();
    try {
        std.array::push(&servers, server);
    } catch (std.array::push_error<std.net::socket_address> rejected) {
        rejected as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
    return move servers;
}

protected bool named(const std.dns::question* asked, str name, u16 kind) {
    return std.text::equal_ignore_ascii_case(asked->name, name) == true && asked->kind == kind;
}

/* The test zone: the records of a question, its response code, and whether an answer over UDP
   is truncated. */
protected u8 zone(const std.dns::question* asked, array<std.dns::record>* answers, bool* truncated)
    throws std.dns::dns_error, std.alloc::alloc_error {
    if (named(asked, "_chat._tcp.test", 33u16) == true) {
        add(answers, std.dns::record::srv(asked->name, 60u32, 20u16, 5u16, 7003u16, "c.test."));
        add(answers, std.dns::record::srv(asked->name, 60u32, 10u16, 1u16, 7001u16, "a.test."));
        add(answers, std.dns::record::txt(asked->name, 60u32, "not a service"));
        add(answers, std.dns::record::srv(asked->name, 60u32, 10u16, 9u16, 7002u16, "b.test."));
        return 0u8;
    }
    if (named(asked, "_none._tcp.test", 33u16) == true) {
        add(answers, std.dns::record::srv(asked->name, 60u32, 0u16, 0u16, 0u16, "."));
        return 0u8;
    }
    if (named(asked, "info.test", 16u16) == true) {
        std.string::string long_text = std.string::create();
        for (u32 index = 0u32; index < 30u32; index += 1u32) {
            std.string::append_str(&long_text, "0123456789");
        }
        add(answers, std.dns::record::txt(asked->name, 60u32, long_text));
        add(answers, std.dns::record::txt(asked->name, 60u32, "v=1"));
        return 0u8;
    }
    if (named(asked, "1.0.0.127.in-addr.arpa", 12u16) == true) {
        add(answers, std.dns::record::ptr(asked->name, 60u32, "localhost."));
        return 0u8;
    }
    if (named(asked, "big.test", 33u16) == true) {
        *truncated = true;
        for (u16 index = 0u16; index < 40u16; index += 1u16) {
            add(answers, std.dns::record::srv(asked->name, 60u32, 1u16, index, (8000u32 + (index as u32)) as u16,
                                              "a-rather-long-target-name.example.test."));
        }
        return 0u8;
    }
    if (named(asked, "broken.test", 33u16) == true) { return 2u8; }
    return 3u8;
}

/* The answer of the zone to a query; over UDP a truncated answer carries no records. */
protected bytes respond(const u8[] query, bool udp) throws std.dns::dns_error, std.alloc::alloc_error {
    std.dns::question asked = std.dns::parse_query(query);
    array<std.dns::record> answers = std.array::create::<std.dns::record>();
    bool truncated = false;
    u8 rcode = zone(&asked, &answers, &truncated);
    if (udp == true && truncated == true) {
        array<std.dns::record> none = std.array::create::<std.dns::record>();
        return std.dns::encode_answer(&asked, &none, rcode, true);
    }
    return std.dns::encode_answer(&asked, &answers, rcode, false);
}

/* Answers count queries over UDP. */
@scoped
protected async void serve_udp(const std.net::udp_socket* socket, u32 count)
    throws std.dns::dns_error, std.error::fault {
    u8[1500] buffer = {};
    for (u32 served = 0u32; served < count; served += 1u32) {
        bytes reply = {};
        task_scope(1) io {
            std.net::datagram arrival = await std.net::udp_receive_into(socket, &buffer);
            bytes answer = respond(buffer[0usize..arrival.count], true);
            bytes old = core::replace(&reply, move answer);
            drop old;
            await std.net::udp_send_from(socket, arrival.peer, reply.as_slice());
        }
    }
}

/* Answers one query over one TCP connection. */
@scoped
protected async void serve_tcp(const std.net::tcp_listener* listener)
    throws std.dns::dns_error, std.error::fault {
    std.net::tcp_connection accepted = await listener->accept();
    u8[2] prefix = {};
    u8[512] query = {};
    bytes framed = {};
    task_scope(1) receive {
        usize have = 0usize;
        while (have < 2usize) {
            have += await std.net::tcp_read_into(&accepted.stream, prefix[have..2usize]);
        }
        usize size = (((prefix[0usize] as u32) << 8u32) | (prefix[1usize] as u32)) as usize;
        usize read = 0usize;
        while (read < size) {
            read += await std.net::tcp_read_into(&accepted.stream, query[read..size]);
        }
        bytes reply = respond(query[0usize..size], false);
        std.bytes::append_u8(&framed, ((len(reply) >> 8usize) & 255usize) as u8);
        std.bytes::append_u8(&framed, (len(reply) & 255usize) as u8);
        std.bytes::append(&framed, reply.as_slice());
    }
    task_scope(1) send { await std.net::tcp_write_all_from(&accepted.stream, framed.as_slice()); }
    (move accepted) as void;
}

protected async std.net::udp_socket open_server() throws std.error::fault {
    std.net::socket_address any_port = loopback(0u16);
    return await any_port.bind(false);
}

protected std.dns::resolver resolver_of(std.net::socket_address server) throws std.alloc::alloc_error {
    std.dns::resolver resolving = std.dns::resolver::with_servers(one(server));
    resolving.set_timeout(std.time::duration_from_seconds(2i64));
    resolving.set_attempts(1u32);
    return move resolving;
}

@test
void encodes_and_reads_a_query() throws std.test::failure, std.dns::dns_error, std.alloc::alloc_error {
    bytes query = std.dns::encode_query(4660u16, "_chat._tcp.Example.org.", 33u16);
    std.test::equal(query[0usize], 18u8);
    std.test::equal(query[1usize], 52u8);
    std.test::equal(query[2usize], 1u8);
    std.test::equal(query[11usize], 1u8);
    std.dns::question asked = std.dns::parse_query(query.as_slice());
    std.test::equal(asked.id, 4660u16);
    std.test::equal_text(asked.name, "_chat._tcp.Example.org");
    std.test::equal(asked.kind, 33u16);
    std.test::check(asked.recursion, "recursion desired");
    std.test::equal(std.dns::type_code(std.dns::record_type::aaaa), 28u16);
}

@test
void rejects_invalid_names() throws std.test::failure, std.alloc::alloc_error {
    std.string::string long_label = std.string::create();
    for (u32 index = 0u32; index < 64u32; index += 1u32) { std.string::append_str(&long_label, "a"); }
    std.string::string long_name = std.string::create();
    for (u32 index = 0u32; index < 26u32; index += 1u32) { std.string::append_str(&long_name, "abcdefghi."); }
    u32 rejected_count = 0u32;
    try {
        bytes query = std.dns::encode_query(1u16, long_label, 1u16);
        (move query) as void;
    } catch (std.dns::dns_error rejected) {
        std.test::check(rejected.code == std.dns::error_code::invalid_name, "a label of 64 bytes");
        rejected_count += 1u32;
    }
    try {
        bytes query = std.dns::encode_query(1u16, long_name, 1u16);
        (move query) as void;
    } catch (std.dns::dns_error rejected) {
        std.test::check(rejected.code == std.dns::error_code::invalid_name, "a name of 260 bytes");
        rejected_count += 1u32;
    }
    try {
        bytes query = std.dns::encode_query(1u16, "a..test", 1u16);
        (move query) as void;
    } catch (std.dns::dns_error rejected) {
        std.test::check(rejected.code == std.dns::error_code::invalid_name, "an empty label");
        rejected_count += 1u32;
    }
    std.test::equal(rejected_count, 3u32);
}

/* A response with id 5 to a.test PTR whose answer names are compressed; bad_pointer points the
   answer's owner name at itself. */
protected bytes compressed_response(bool bad_pointer) throws std.alloc::alloc_error {
    u8[36] message = {0u8, 5u8, 129u8, 128u8, 0u8, 1u8, 0u8, 1u8, 0u8, 0u8, 0u8, 0u8,
                      1u8, 97u8, 4u8, 116u8, 101u8, 115u8, 116u8, 0u8, 0u8, 12u8, 0u8, 1u8,
                      192u8, 12u8, 0u8, 12u8, 0u8, 1u8, 0u8, 0u8, 0u8, 60u8, 0u8, 4u8};
    bytes result = {};
    std.bytes::append(&result, &message);
    if (bad_pointer == true) {
        result[25usize] = 24u8;
    }
    std.bytes::append_u8(&result, 1u8);
    std.bytes::append_u8(&result, 120u8);
    std.bytes::append_u8(&result, 192u8);
    std.bytes::append_u8(&result, 14u8);
    return move result;
}

@test
void follows_compressed_names() throws std.test::failure, std.dns::dns_error, std.alloc::alloc_error {
    bytes message = compressed_response(false);
    array<std.dns::record> found = std.dns::parse_response(message.as_slice(), 5u16, "A.TEST.", 12u16);
    std.test::equal(len(found), 1usize);
    std.test::equal_text(found[0usize].name, "a.test");
    std.test::equal_text(found[0usize].target, "x.test");
    std.test::equal(found[0usize].ttl, 60u32);
    u32 failures = 0u32;
    try {
        array<std.dns::record> other = std.dns::parse_response(message.as_slice(), 6u16, "a.test", 12u16);
        (move other) as void;
    } catch (std.dns::dns_error rejected) {
        std.test::check(rejected.code == std.dns::error_code::malformed_response, "another id");
        failures += 1u32;
    }
    try {
        array<std.dns::record> other = std.dns::parse_response(message.as_slice(), 5u16, "b.test", 12u16);
        (move other) as void;
    } catch (std.dns::dns_error rejected) {
        std.test::check(rejected.code == std.dns::error_code::malformed_response, "another name");
        failures += 1u32;
    }
    bytes looping = compressed_response(true);
    try {
        array<std.dns::record> other = std.dns::parse_response(looping.as_slice(), 5u16, "a.test", 12u16);
        (move other) as void;
    } catch (std.dns::dns_error rejected) {
        std.test::check(rejected.code == std.dns::error_code::malformed_response, "a forward pointer");
        failures += 1u32;
    }
    bytes cut = {};
    std.bytes::append(&cut, message[0usize..30usize]);
    try {
        array<std.dns::record> other = std.dns::parse_response(cut.as_slice(), 5u16, "a.test", 12u16);
        (move other) as void;
    } catch (std.dns::dns_error rejected) {
        std.test::check(rejected.code == std.dns::error_code::malformed_response, "a cut message");
        failures += 1u32;
    }
    std.test::equal(failures, 4u32);
}

@test
void reads_resolv_conf() throws std.test::failure, std.alloc::alloc_error {
    array<std.net::socket_address> servers = std.dns::parse_resolv_conf(
        "# generated\nsearch example.org\nnameserver 192.0.2.53\n  nameserver\t2001:db8::1%en0\n"
        "nameserver not-an-address\nnameservers 10.0.0.1\noptions ndots:2\n");
    std.test::equal(len(servers), 2usize);
    std.string::string first = std.net::format_ip(servers[0usize].address);
    std.string::string second = std.net::format_ip(servers[1usize].address);
    std.test::equal_text(first, "192.0.2.53");
    std.test::equal_text(second, "2001:db8::1");
    std.test::equal(servers[1usize].port, 53u16);
}

@test
void names_reverse_lookups() throws std.test::failure, std.error::fault {
    std.string::string v4 = std.dns::reverse_name(std.net::parse_ip("192.0.2.10"));
    std.test::equal_text(v4, "10.2.0.192.in-addr.arpa");
    std.string::string v6 = std.dns::reverse_name(std.net::parse_ip("2001:db8::567:89ab"));
    std.test::equal_text(v6,
                         "b.a.9.8.7.6.5.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.0.8.b.d.0.1.0.0.2.ip6.arpa");
}

@test
async void finds_services_in_order() throws std.dns::dns_error, std.error::fault, std.test::failure {
    std.net::udp_socket socket = await open_server();
    std.dns::resolver resolving = resolver_of(socket.local_address());
    task_scope(2) group {
        auto server = serve_udp(&socket, 2u32);
        array<std.dns::srv_record> services = await resolving.lookup_srv("_chat._tcp.test");
        std.test::equal(len(services), 3usize);
        std.test::equal_text(services[0usize].target, "b.test");
        std.test::equal(services[0usize].port, 7002u16);
        std.test::equal_text(services[1usize].target, "a.test");
        std.test::equal(services[1usize].weight, 1u16);
        std.test::equal_text(services[2usize].target, "c.test");
        std.test::equal(services[2usize].priority, 20u16);
        array<std.dns::srv_record> none = await resolving.lookup_srv("_none._tcp.test");
        std.test::equal(len(none), 0usize);
        await move server;
    }
    await (move socket).close();
}

@test
async void reads_text_and_reverse_records() throws std.dns::dns_error, std.error::fault, std.test::failure {
    std.net::udp_socket socket = await open_server();
    std.dns::resolver resolving = resolver_of(socket.local_address());
    task_scope(2) group {
        auto server = serve_udp(&socket, 2u32);
        array<std.dns::txt_record> texts = await resolving.lookup_txt("info.test");
        std.test::equal(len(texts), 2usize);
        std.test::equal(len(texts[0usize].strings), 2usize);
        std.test::equal(len(texts[0usize].strings[0usize]), 255usize);
        std.string::string joined = texts[0usize].text();
        std.test::equal(std.string::len(&joined), 300usize);
        std.string::string short_text = texts[1usize].text();
        std.test::equal_text(short_text, "v=1");
        array<std.string::string> names = await resolving.reverse(std.net::parse_ip("127.0.0.1"));
        std.test::equal(len(names), 1usize);
        std.test::equal_text(names[0usize], "localhost");
        await move server;
    }
    await (move socket).close();
}

@test
async void asks_again_over_tcp_when_truncated() throws std.dns::dns_error, std.error::fault, std.test::failure {
    std.net::udp_socket socket = await open_server();
    std.net::socket_address endpoint = socket.local_address();
    std.net::listen_options settings = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
    std.net::tcp_listener listener = await endpoint.listen(settings);
    std.dns::resolver resolving = resolver_of(endpoint);
    task_scope(3) group {
        auto datagrams = serve_udp(&socket, 1u32);
        auto connection = serve_tcp(&listener);
        array<std.dns::srv_record> services = await resolving.lookup_srv("big.test");
        std.test::equal(len(services), 40usize);
        std.test::equal(services[0usize].weight, 39u16);
        std.test::equal(services[39usize].port, 8000u16);
        await move datagrams;
        await move connection;
    }
    await (move listener).close();
    await (move socket).close();
}

@test
async void reports_answer_codes_and_timeouts() throws std.dns::dns_error, std.error::fault, std.test::failure {
    std.net::udp_socket socket = await open_server();
    std.dns::resolver resolving = resolver_of(socket.local_address());
    u32 failures = 0u32;
    task_scope(2) group {
        auto server = serve_udp(&socket, 2u32);
        try {
            array<std.dns::srv_record> services = await resolving.lookup_srv("missing.test");
            (move services) as void;
        } catch (std.dns::dns_error rejected) {
            std.test::check(rejected.code == std.dns::error_code::name_error, "name_error");
            failures += 1u32;
        }
        try {
            array<std.dns::srv_record> services = await resolving.lookup_srv("broken.test");
            (move services) as void;
        } catch (std.dns::dns_error rejected) {
            std.test::check(rejected.code == std.dns::error_code::server_failure, "server_failure");
            failures += 1u32;
        }
        await move server;
    }
    std.dns::resolver silent = resolver_of(socket.local_address());
    silent.set_timeout(std.time::duration_from_parts(0i64, 100000000u32));
    silent.set_attempts(2u32);
    std.time::instant started = std.time::monotonic_now();
    task_scope(1) io {
        try {
            array<std.dns::srv_record> services = await silent.lookup_srv("_chat._tcp.test");
            (move services) as void;
        } catch (std.dns::dns_error rejected) {
            std.test::check(rejected.code == std.dns::error_code::timed_out, "timed_out");
            failures += 1u32;
        }
    }
    std.time::duration spent = std.time::instant_duration(std.time::monotonic_now(), started);
    i64 spent_ms = std.time::duration_seconds(spent) * 1000i64 +
                   (std.time::duration_nanoseconds(spent) / 1000000u32) as i64;
    std.test::check(spent_ms >= 150i64, "two attempts of 100 ms");
    array<std.net::socket_address> empty = std.array::create::<std.net::socket_address>();
    std.dns::resolver nobody = std.dns::resolver::with_servers(move empty);
    task_scope(1) io {
        try {
            array<std.dns::srv_record> services = await nobody.lookup_srv("_chat._tcp.test");
            (move services) as void;
        } catch (std.dns::dns_error rejected) {
            std.test::check(rejected.code == std.dns::error_code::no_servers, "no_servers");
            failures += 1u32;
        }
    }
    std.test::equal(failures, 4u32);
    await (move socket).close();
}
