module example.tls.peers;
import std.net;
import std.tls;

/* The largest certificate or key file the example reads. */
const usize max_pem = 65536usize;

protected async bytes read_pem(std.string::string name) throws std.error::fault {
    std.fs::path path = std.fs::path_from_utf8(name.as_str());
    return await path.read_file(max_pem);
}

/* A client that trusts the authorities of the PEM file ca, or with `system` those of the system
   (SSL_CERT_FILE or the bundle of the system), and offers two protocols. */
async std.tls::config client(std.string::string ca) throws std.error::fault, std.tls::tls_error {
    switch (ca.as_str()) {
    case "system":
        std.tls::config trusted = await std.tls::system_client_config();
        trusted.add_protocol("h2");
        trusted.add_protocol("echo/1");
        return move trusted;
    default: break;
    }
    bytes authority = await read_pem(move ca);
    std.tls::config settings = std.tls::client_config();
    settings.add_authority(authority.as_slice());
    settings.add_protocol("h2");
    settings.add_protocol("echo/1");
    return move settings;
}

/* A server presenting the chain of the PEM file cert with the key of the PEM file key. */
async std.tls::config server(std.string::string cert, std.string::string key)
    throws std.error::fault, std.tls::tls_error {
    bytes chain = await read_pem(move cert);
    bytes secret = await read_pem(move key);
    std.secret::buffer private_key = std.secret::from_bytes(move secret);
    std.tls::config settings = std.tls::server_config();
    settings.set_identity(chain.as_slice(), &private_key);
    settings.add_protocol("echo/1");
    return move settings;
}

/* A listener on an unused port of the loopback interface. */
async std.net::tcp_listener listen_loopback() throws std.error::fault {
    std.net::socket_address local = std.net::socket_address {
        .address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 1u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

/* Reads one request, answers it with its length and waits for close_notify. */
async void answer(std.tls::stream<std.net::tcp_connection> session) throws std.error::fault {
    u8[1024] request = {};
    usize count = 0usize;
    task_scope(1) io { count += await session.read_into(&request); }
    /* close_notify before any request: the client only checked the handshake. */
    if (count == 0usize) { return; }
    std.string::string text = std.string::from_str("");
    text.append_utf8(request[0usize..count]);
    std.string::string reply = f"{count} bytes: {text}";
    task_scope(1) io {
        await session.write_all_from(reply.as_bytes());
        while (true) {
            usize more = await session.read_into(&request);
            if (more == 0usize) { break; }
        }
    }
}

/* The server side: completes its handshake and answers. A failed handshake ends the server
   quietly: the client reports it. */
@scoped
async void serve(std.net::tcp_connection transport, const std.tls::config* settings)
    throws std.error::fault {
    try {
        task_scope(1) io {
            std.tls::stream<std.net::tcp_connection> session =
                await std.tls::accept(move transport, settings);
            await answer(move session);
        }
    } catch (std.tls::tls_error failure) {
        failure as void;
    }
}
