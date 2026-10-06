module example.realtime.names;
import std.dns;
import std.text;

protected void add(array<std.dns::record>* answers, std.dns::record entry) throws std.alloc::alloc_error {
    try {
        answers->push(move entry);
    } catch (std.array::push_error<std.dns::record> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* The zone of the example: the chat service _chat._tcp.realtime.test on the port of the HTTP
   server at localhost, its TXT record with the path and the subprotocol, and the name of
   127.0.0.1. Other questions have no answer (NXDOMAIN). */
protected u8 zone(const std.dns::question* asked, u16 port, array<std.dns::record>* answers)
    throws std.dns::dns_error, std.alloc::alloc_error {
    str name = asked->name;
    bool service = std.text::equal_ignore_ascii_case(name, "_chat._tcp.realtime.test");
    if (service == true && asked->kind == std.dns::type_code(std.dns::record_type::srv)) {
        add(answers, std.dns::record::srv(name, 60u32, 10u16, 5u16, port, "localhost."));
        return 0u8;
    }
    if (service == true && asked->kind == std.dns::type_code(std.dns::record_type::txt)) {
        add(answers, std.dns::record::txt(name, 60u32, "path=/chat"));
        add(answers, std.dns::record::txt(name, 60u32, "protocol=chat"));
        return 0u8;
    }
    if (std.text::equal_ignore_ascii_case(name, "1.0.0.127.in-addr.arpa") == true &&
        asked->kind == std.dns::type_code(std.dns::record_type::ptr)) {
        add(answers, std.dns::record::ptr(name, 60u32, "localhost."));
        return 0u8;
    }
    return 3u8;
}

/* Opens the name server on a free UDP port of the loopback interface. */
async std.net::udp_socket open() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    return await local.bind(false);
}

/* Answers count questions over UDP; the chat service is on port. */
@scoped
async void serve(const std.net::udp_socket* socket, u16 port, u32 count)
    throws std.dns::dns_error, std.error::fault {
    u8[1500] buffer = {};
    for (u32 served = 0u32; served < count; served += 1u32) {
        bytes reply = {};
        task_scope(1) io {
            std.net::datagram arrival = await std.net::udp_receive_into(socket, &buffer);
            std.dns::question asked = std.dns::parse_query(buffer[0usize..arrival.count]);
            array<std.dns::record> answers = std.array::create::<std.dns::record>();
            u8 rcode = zone(&asked, port, &answers);
            bytes answer = std.dns::encode_answer(&asked, &answers, rcode, false);
            bytes old = core::replace(&reply, move answer);
            drop old;
            await std.net::udp_send_from(socket, arrival.peer, reply.as_slice());
        }
    }
}
