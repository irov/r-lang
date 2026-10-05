# TLS over loopback TCP

Secure a TCP connection with TLS 1.3: `std.tls` (Library R-SLIB-TLS-0001..0007) runs a client
and a server session over any `std.stream::Stream`, verifies the certificate chain and the name
of the server, negotiates an application protocol by ALPN and is itself a stream. The engine is
Mbed TLS 3.6, which the R part of `std.tls` calls through the checked C boundary.

```sh
ctest --test-dir build/debug -R 'example_tls' --output-on-failure
F=tests/fixtures/tls
build/debug/tests/codegen_example_tls echo $F/authority.pem $F/server.pem $F/server_key.pem localhost hello world
build/debug/tests/codegen_example_tls check $F/other_authority.pem $F/server.pem $F/server_key.pem localhost
build/debug/tests/codegen_example_tls check system $F/server.pem $F/server_key.pem localhost
SSL_CERT_FILE=$F/authority.pem build/debug/tests/codegen_example_tls check system $F/server.pem $F/server_key.pem localhost
```

Both commands start a server and a client in one process on a free loopback port. The client
trusts the authorities of `CA` and expects the certificate to name `NAME`; the server presents
`CERT` with the private key `KEY`, which is read into a `std.secret::buffer`:

```r
std.tls::config settings = std.tls::client_config();
settings.add_authority(authority.as_slice());
settings.add_protocol("h2");
settings.add_protocol("echo/1");
```

The server accepts in a task of the same group while the client connects; both run their
handshakes over their own ends of the TCP connection:

```r
task_scope(2) io {
    task<void throws std.error::fault> serving =
        example.tls.peers::serve(move incoming, &server);
    try {
        std.tls::stream<std.net::tcp_stream> session =
            await std.tls::connect(move outgoing, &client, arguments[5].as_str());
        await talk(move session, command, message(&arguments));
    } catch (std.tls::tls_error failure) {
        status = 65;
        await std.console::println(f"rejected: {failure.code}");
    }
    await move serving;
}
```

`echo` sends the words and prints the answer of the server, which counts the bytes it read:

```text
trusted: protocol=echo/1 version=tls13
reply: 11 bytes: hello world
```

`check` only completes the handshake and sends close_notify. A certificate that names another
host, comes from an authority the client does not trust or is no longer valid ends the
handshake with `name_mismatch`, `untrusted_certificate` or `expired_certificate` and status 65:

```text
rejected: untrusted_certificate
```

With `system` in place of the authority file the client trusts the certificate authorities of the
system through `std.tls::system_client_config()` (R-SLIB-TLS-0007): the PEM bundle that
`SSL_CERT_FILE` names, or else the bundle file of the system, `/etc/ssl/cert.pem` on macOS. The
test authority is not among the authorities of the system, so the first `system` command above
ends with `rejected: untrusted_certificate`; with `SSL_CERT_FILE` naming that authority the
handshake is trusted. A bundle without any certificate is `tls: missing_authorities`:

```r
std.tls::config trusted = await std.tls::system_client_config();
trusted.add_protocol("h2");
trusted.add_protocol("echo/1");
```

A file that holds no certificate or no key is reported as `tls: invalid_certificate` or
`tls: invalid_key` on standard error, also with status 65. The certificates of
`tests/fixtures/tls` are EC P-256 test certificates of the authority "R Test CA";
`server.pem` names `localhost` and `127.0.0.1`.
