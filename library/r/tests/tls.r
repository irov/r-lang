module tests.std.tls;
import std.test;
import std.net;
import std.tls;

// The tests of std.tls (Library R-SLIB-TLS-0001..0006): a client and a server session over a
// loopback TCP connection negotiate TLS 1.3 and ALPN and exchange application data up to
// close_notify; certificate verification rejects a wrong name, an unknown authority and an
// expired certificate; a configuration rejects invalid material, freezes with its first session
// and reports each failing allocation of the provider; the end of the transport during the
// handshake fails it. Run in test mode (Core
// R-FUNC-0025) with the native provider of std.tls linked.

/* The test authority "R Test CA" (EC P-256) that issued the server certificates. */
const constexpr str AUTHORITY =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBkDCCATWgAwIBAgIUapLhhLDwmYV1Rwi43Y/ZhIFkSpowCgYIKoZIzj0EAwIw\n"
    "FDESMBAGA1UEAwwJUiBUZXN0IENBMCAXDTI1MDEwMTAwMDAwMFoYDzIxMjUwMTAx\n"
    "MDAwMDAwWjAUMRIwEAYDVQQDDAlSIFRlc3QgQ0EwWTATBgcqhkjOPQIBBggqhkjO\n"
    "PQMBBwNCAAQ+S5UBiZd/pzQwPA5hAeohScE19KAxopyXfRlmCO1EclFKdu2sdR16\n"
    "Z8hd/B6JLqRxYsFdWodr4YUazHg8gxZbo2MwYTAdBgNVHQ4EFgQUkOaW7C/+j62Z\n"
    "A7b23fhYw5e2NzgwHwYDVR0jBBgwFoAUkOaW7C/+j62ZA7b23fhYw5e2NzgwDwYD\n"
    "VR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwCgYIKoZIzj0EAwIDSQAwRgIh\n"
    "AJlGe7nQxNIqJVDH0gJijaOgt00j5Mi4vZQFDyJJntnGAiEA9NJSvEBqM8BlDipd\n"
    "vvbNilGBCu+SIb2DldIkJkkFMNA=\n"
    "-----END CERTIFICATE-----\n";

/* An authority that issued none of them. */
const constexpr str OTHER_AUTHORITY =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBkjCCATegAwIBAgIUWLpkuHhG/kx+gjsc3pu5eJRBplQwCgYIKoZIzj0EAwIw\n"
    "FTETMBEGA1UEAwwKUiBPdGhlciBDQTAgFw0yNTAxMDEwMDAwMDBaGA8yMTI1MDEw\n"
    "MTAwMDAwMFowFTETMBEGA1UEAwwKUiBPdGhlciBDQTBZMBMGByqGSM49AgEGCCqG\n"
    "SM49AwEHA0IABEGO6wxPW7tN569jkMNDICZzUbLWWOm43D9eLTwsdhZ3o1Yu4ZZx\n"
    "xQufQ+0wQESmthN+OQQPmDp8RYuUq1H0mqGjYzBhMB0GA1UdDgQWBBTQ4GDqZRkL\n"
    "+J0msUZj0r8krIu8DzAfBgNVHSMEGDAWgBTQ4GDqZRkL+J0msUZj0r8krIu8DzAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBBjAKBggqhkjOPQQDAgNJADBG\n"
    "AiEAi0o8OfteY2jwSk39AKQPlxXe6DUJ1kU6HO/0HjdWZgcCIQCE3r6NH+uEd+Kw\n"
    "MHuO9vl+gFs9Wqn2UZIXxgV+aNUJbQ==\n"
    "-----END CERTIFICATE-----\n";

/* CN=localhost with DNS:localhost and IP:127.0.0.1, valid 2025..2125. */
const constexpr str SERVER_CERTIFICATE =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBqTCCAU+gAwIBAgIBAjAKBggqhkjOPQQDAjAUMRIwEAYDVQQDDAlSIFRlc3Qg\n"
    "Q0EwIBcNMjUwMTAxMDAwMDAwWhgPMjEyNTAxMDEwMDAwMDBaMBQxEjAQBgNVBAMM\n"
    "CWxvY2FsaG9zdDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABJj7DUw+i1Lb7ymP\n"
    "eHpocSyk2KyNbnMi40Jjr2/2EbuVioKl+/8R6XXzjurdLrB8ecySjj3WhhAXIW2R\n"
    "gIOFT9ejgY8wgYwwCQYDVR0TBAIwADAOBgNVHQ8BAf8EBAMCB4AwEwYDVR0lBAww\n"
    "CgYIKwYBBQUHAwEwGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMB0GA1UdDgQW\n"
    "BBR0kVv3v5fYCoPhs/hbu3JWWIYYHTAfBgNVHSMEGDAWgBSQ5pbsL/6PrZkDtvbd\n"
    "+FjDl7Y3ODAKBggqhkjOPQQDAgNIADBFAiBnEmKpaZ784hn4T42fsKtgE4vE5nt3\n"
    "7Kg/TrcMfiVMfAIhAKbPXo+gxuc0rJWbA1pTX3OFcZjjd6m0e5FNLZn8gYNA\n"
    "-----END CERTIFICATE-----\n";

/* The same subject and key, valid only in the past. */
const constexpr str EXPIRED_CERTIFICATE =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBqDCCAU2gAwIBAgIBAzAKBggqhkjOPQQDAjAUMRIwEAYDVQQDDAlSIFRlc3Qg\n"
    "Q0EwHhcNMjAwMTAxMDAwMDAwWhcNMjEwMTAxMDAwMDAwWjAUMRIwEAYDVQQDDAls\n"
    "b2NhbGhvc3QwWTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAASY+w1MPotS2+8pj3h6\n"
    "aHEspNisjW5zIuNCY69v9hG7lYqCpfv/Eel1847q3S6wfHnMko491oYQFyFtkYCD\n"
    "hU/Xo4GPMIGMMAkGA1UdEwQCMAAwDgYDVR0PAQH/BAQDAgeAMBMGA1UdJQQMMAoG\n"
    "CCsGAQUFBwMBMBoGA1UdEQQTMBGCCWxvY2FsaG9zdIcEfwAAATAdBgNVHQ4EFgQU\n"
    "dJFb97+X2AqD4bP4W7tyVliGGB0wHwYDVR0jBBgwFoAUkOaW7C/+j62ZA7b23fhY\n"
    "w5e2NzgwCgYIKoZIzj0EAwIDSQAwRgIhAJ1YZdPd+8UBnq7aTq2Eyfj7wWDqh8+r\n"
    "4H4ohUmssVlCAiEAllrPpTLseIUYq3LzS361Dw0c2gxCvQ2uswhY19K+gB4=\n"
    "-----END CERTIFICATE-----\n";

/* The private key of both server certificates. */
const constexpr str SERVER_KEY =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIMz6l1l6INXhtQIwqD+ZIuvL6SuWr+L9f8QfDoMb6LROoAoGCCqGSM49\n"
    "AwEHoUQDQgAEmPsNTD6LUtvvKY94emhxLKTYrI1ucyLjQmOvb/YRu5WKgqX7/xHp\n"
    "dfOO6t0usHx5zJKOPdaGEBchbZGAg4VP1w==\n"
    "-----END EC PRIVATE KEY-----\n";

protected std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                    .scope_id = 0u32};
}

protected std.tls::config client_settings(str authority) throws std.tls::tls_error, std.alloc::alloc_error {
    std.tls::config settings = std.tls::client_config();
    settings.add_authority(authority);
    settings.add_protocol("h2");
    settings.add_protocol("http/1.1");
    return move settings;
}

protected std.tls::config server_settings(str certificate) throws std.tls::tls_error, std.alloc::alloc_error {
    std.tls::config settings = std.tls::server_config();
    std.string::string text = std.string::from_str(SERVER_KEY);
    std.secret::buffer key = std.secret::from_bytes((move text).into_bytes());
    settings.set_identity(certificate, &key);
    settings.add_protocol("http/1.1");
    return move settings;
}

/* A listener on an unused port of the loopback interface. */
protected async std.net::tcp_listener listen_local() throws std.error::fault {
    std.net::listen_options options = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
    std.net::socket_address local = loopback();
    return await local.listen(options);
}

/* Application data both ways, then close_notify ends the stream for the server. */
protected async void exchange(std.tls::stream<std.net::tcp_stream> secured,
                              std.tls::stream<std.net::tcp_connection> served)
    throws std.error::fault, std.test::failure {
    str negotiated = secured.protocol();
    std.test::equal_text(negotiated, "http/1.1");
    str chosen = served.protocol();
    std.test::equal_text(chosen, "http/1.1");
    std.test::check(secured.version() == std.tls::version::tls13, "TLS 1.3");
    std.string::string request = std.string::from_str("hello over tls");
    std.string::string pong = std.string::from_str("pong");
    u8[64] buffer = {};
    u8[16] reply = {};
    task_scope(1) io {
        await secured.write_all_from(request.as_bytes());
        usize count = await served.read_into(&buffer);
        std.test::check(std.bytes::equal(buffer[0usize..count], "hello over tls"), "the request");
        await served.write_all_from(pong.as_bytes());
        usize back = await secured.read_into(&reply);
        std.test::check(std.bytes::equal(reply[0usize..back], "pong"), "the reply");
        await secured.shutdown();
        usize end = await served.read_into(&buffer);
        std.test::equal(end, 0usize);
    }
}

@test
async void exchanges_data_over_tls() throws std.error::fault, std.test::failure, std.tls::tls_error {
    std.tls::config client = client_settings(AUTHORITY);
    std.tls::config server = server_settings(SERVER_CERTIFICATE);
    std.net::tcp_listener listener = await listen_local();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream outgoing = await endpoint.connect();
    std.net::tcp_connection incoming = await listener.accept();
    await (move listener).close();
    task_scope(2) io {
        task<std.tls::stream<std.net::tcp_connection> throws std.tls::tls_error, std.error::fault>
            accepting = std.tls::accept(move incoming, &server);
        std.tls::stream<std.net::tcp_stream> secured =
            await std.tls::connect(move outgoing, &client, "localhost");
        std.tls::stream<std.net::tcp_connection> served = await move accepting;
        await exchange(move secured, move served);
    }
}

/* The error that fails the client handshake; the test fails when the handshake succeeds. */
@scoped
protected async std.tls::error_code client_failure(std.net::tcp_stream transport,
                                                   const std.tls::config* settings,
                                                   str server_name)
    throws std.error::fault, std.test::failure {
    try {
        task_scope(1) io {
            std.tls::stream<std.net::tcp_stream> secured =
                await std.tls::connect(move transport, settings, server_name);
            drop secured;
        }
    } catch (std.tls::tls_error failure) {
        return failure.code;
    }
    std.test::fail("the client handshake succeeded");
    return std.tls::error_code::closed;
}

/* Whether the server handshake failed, as it does when its client rejects it. */
@scoped
protected async bool server_fails(std.net::tcp_connection transport, const std.tls::config* settings)
    throws std.error::fault {
    try {
        task_scope(1) io {
            std.tls::stream<std.net::tcp_connection> served =
                await std.tls::accept(move transport, settings);
            drop served;
        }
    } catch (std.tls::tls_error failure) {
        failure as void;
        return true;
    }
    return false;
}

/* The client error of a handshake between a client trusting authority and expecting
   server_name and a server presenting certificate; the server fails too. */
@scoped
protected async std.tls::error_code rejection(str authority, str certificate, str server_name)
    throws std.error::fault, std.test::failure, std.tls::tls_error {
    std.tls::config client = client_settings(authority);
    std.tls::config server = server_settings(certificate);
    std.net::tcp_listener listener = await listen_local();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream outgoing = await endpoint.connect();
    std.net::tcp_connection incoming = await listener.accept();
    await (move listener).close();
    task_scope(2) io {
        task<bool throws std.error::fault> serving = server_fails(move incoming, &server);
        std.tls::error_code code = await client_failure(move outgoing, &client, server_name);
        bool failed = await move serving;
        std.test::check(failed, "the server handshake fails with the client");
        return code;
    }
}

@test
async void rejects_a_certificate_for_another_name()
    throws std.error::fault, std.test::failure, std.tls::tls_error {
    task_scope(1) io {
        std.tls::error_code code = await rejection(AUTHORITY, SERVER_CERTIFICATE, "example.org");
        std.test::check(code == std.tls::error_code::name_mismatch, "name_mismatch");
    }
}

@test
async void accepts_an_address_in_the_certificate()
    throws std.error::fault, std.test::failure, std.tls::tls_error {
    std.tls::config client = client_settings(AUTHORITY);
    std.tls::config server = server_settings(SERVER_CERTIFICATE);
    std.net::tcp_listener listener = await listen_local();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream outgoing = await endpoint.connect();
    std.net::tcp_connection incoming = await listener.accept();
    await (move listener).close();
    task_scope(2) io {
        task<std.tls::stream<std.net::tcp_connection> throws std.tls::tls_error, std.error::fault>
            accepting = std.tls::accept(move incoming, &server);
        std.tls::stream<std.net::tcp_stream> secured =
            await std.tls::connect(move outgoing, &client, "127.0.0.1");
        std.tls::stream<std.net::tcp_connection> served = await move accepting;
        await exchange(move secured, move served);
    }
}

@test
async void rejects_an_unknown_authority()
    throws std.error::fault, std.test::failure, std.tls::tls_error {
    task_scope(1) io {
        std.tls::error_code code = await rejection(OTHER_AUTHORITY, SERVER_CERTIFICATE, "localhost");
        std.test::check(code == std.tls::error_code::untrusted_certificate, "untrusted_certificate");
    }
}

@test
async void rejects_an_expired_certificate()
    throws std.error::fault, std.test::failure, std.tls::tls_error {
    task_scope(1) io {
        std.tls::error_code code = await rejection(AUTHORITY, EXPIRED_CERTIFICATE, "localhost");
        std.test::check(code == std.tls::error_code::expired_certificate, "expired_certificate");
    }
}

@test
async void skips_verification_when_asked()
    throws std.error::fault, std.test::failure, std.tls::tls_error {
    std.tls::config client = client_settings(OTHER_AUTHORITY);
    client.set_verification(false);
    std.tls::config server = server_settings(SERVER_CERTIFICATE);
    std.net::tcp_listener listener = await listen_local();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream outgoing = await endpoint.connect();
    std.net::tcp_connection incoming = await listener.accept();
    await (move listener).close();
    task_scope(2) io {
        task<std.tls::stream<std.net::tcp_connection> throws std.tls::tls_error, std.error::fault>
            accepting = std.tls::accept(move incoming, &server);
        std.tls::stream<std.net::tcp_stream> secured =
            await std.tls::connect(move outgoing, &client, "example.org");
        std.tls::stream<std.net::tcp_connection> served = await move accepting;
        await exchange(move secured, move served);
    }
}

/* A peer that reads the ClientHello, ends its write direction without answering and reads
   until the client closes, so that the client sees the end of the transport. */
protected async void end_after_hello(std.net::tcp_connection transport) throws std.error::fault {
    u8[4096] buffer = {};
    task_scope(1) io {
        usize hello = await transport.read_into(&buffer);
        hello as void;
        await transport.shutdown();
        while (true) {
            usize count = await transport.read_into(&buffer);
            if (count == 0usize) { break; }
        }
    }
}

/* The client error of a handshake whose transport ends before the server answers. */
@scoped
protected async std.tls::error_code ended_handshake(const std.tls::config* client)
    throws std.error::fault, std.test::failure {
    std.net::tcp_listener listener = await listen_local();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream outgoing = await endpoint.connect();
    std.net::tcp_connection incoming = await listener.accept();
    await (move listener).close();
    task_scope(2) io {
        task<void throws std.error::fault> peer = end_after_hello(move incoming);
        std.tls::error_code code = await client_failure(move outgoing, client, "localhost");
        await move peer;
        return code;
    }
}

@test
async void fails_when_the_transport_ends_in_the_handshake()
    throws std.error::fault, std.test::failure, std.tls::tls_error {
    std.tls::config client = client_settings(AUTHORITY);
    task_scope(1) io {
        std.tls::error_code code = await ended_handshake(&client);
        std.test::check(code == std.tls::error_code::handshake_failed, "handshake_failed");
    }
}

@test
void rejects_invalid_material() throws std.test::failure, std.alloc::alloc_error {
    std.tls::config settings = std.tls::server_config();
    try {
        settings.add_authority("not a certificate");
        std.test::fail("the text is no certificate");
    } catch (std.tls::tls_error failure) {
        std.test::check(failure.code == std.tls::error_code::invalid_certificate, "invalid_certificate");
        std.test::check(failure.native_code < 0i64, "the Mbed TLS error");
    }
    std.string::string text = std.string::from_str("not a key");
    std.secret::buffer key = std.secret::from_bytes((move text).into_bytes());
    try {
        settings.set_identity(SERVER_CERTIFICATE, &key);
        std.test::fail("the text is no key");
    } catch (std.tls::tls_error failure) {
        std.test::check(failure.code == std.tls::error_code::invalid_key, "invalid_key");
    }
    try {
        settings.add_protocol("");
        std.test::fail("an empty protocol name");
    } catch (std.tls::tls_error failure) {
        std.test::check(failure.code == std.tls::error_code::invalid_argument, "invalid_argument");
    }
}

@test
async void freezes_a_configuration_with_its_first_session()
    throws std.error::fault, std.test::failure, std.tls::tls_error {
    std.tls::config client = client_settings(AUTHORITY);
    task_scope(1) io {
        std.tls::error_code code = await ended_handshake(&client);
        code as void;
    }
    try {
        client.add_protocol("h3");
        std.test::fail("a frozen configuration changed");
    } catch (std.tls::tls_error failure) {
        std.test::check(failure.code == std.tls::error_code::frozen, "frozen");
    }
}

@test(allocations)
void configurations_report_exhausted_memory()
    throws std.test::failure, std.tls::tls_error, std.alloc::alloc_error {
    std.tls::config client = client_settings(AUTHORITY);
    std.tls::config server = server_settings(SERVER_CERTIFICATE);
    drop client;
    drop server;
}
