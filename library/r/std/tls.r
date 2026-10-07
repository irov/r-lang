module std.tls;
import std.stream;

/* R-SLIB-TLS-0001: the native provider of the module, an engine over Mbed TLS 3.6 that
   exchanges records through memory (Core R-FFI-0028..0044). Its configurations and sessions
   are internally synchronized, so the owners of this module hold their addresses in atomic
   fields, which are Send and Sync (Core R-MEM-0003). */
@link(name = "std.tls.native", kind = "static")
@header("r_std_tls_native.h")
extern "C" {
    @safety("TLS-CONFIG-CREATE", "The function has no preconditions")
    raw void*? r_std_tls_native_config_create(c_int32 server);

    @safety("TLS-CONFIG-RELEASE", "The configuration is live and this reference ends once")
    void r_std_tls_native_config_release(raw void*? config);

    @safety("TLS-CONFIG-AUTHORITY",
            "The configuration is live, data addresses length bytes and native is writable")
    c_int32 r_std_tls_native_config_add_authority(raw void*? config,
                                                  raw const c_uint8*? data,
                                                  c_size length,
                                                  raw c_int64* native);

    @safety("TLS-CONFIG-IDENTITY",
            "The configuration is live, chain and key address their lengths and native is writable")
    c_int32 r_std_tls_native_config_set_identity(raw void*? config,
                                                 raw const c_uint8*? chain,
                                                 c_size chain_length,
                                                 raw const c_uint8*? key,
                                                 c_size key_length,
                                                 raw c_int64* native);

    @safety("TLS-CONFIG-PROTOCOL", "The configuration is live and name addresses length bytes")
    c_int32 r_std_tls_native_config_add_protocol(raw void*? config,
                                                 raw const c_uint8*? name,
                                                 c_size length);

    @safety("TLS-CONFIG-VERIFICATION", "The configuration is live")
    c_int32 r_std_tls_native_config_set_verification(raw void*? config, c_int32 required);

    @safety("TLS-SESSION-CREATE",
            "The configuration is live, the name addresses its length and the outputs are writable")
    raw void*? r_std_tls_native_session_create(raw void*? config,
                                               raw const c_uint8*? server_name,
                                               c_size server_name_length,
                                               raw c_int32* status,
                                               raw c_int64* native);

    @safety("TLS-SESSION-RELEASE", "The session is live and released once")
    void r_std_tls_native_session_release(raw void*? session);

    @safety("TLS-SESSION-FEED", "The session is live and data addresses length bytes")
    c_int32 r_std_tls_native_session_feed(raw void*? session, raw const c_uint8*? data, c_size length);

    @safety("TLS-SESSION-TAKE", "The session is live and target addresses capacity writable bytes")
    c_size r_std_tls_native_session_take(raw void*? session, raw c_uint8* target, c_size capacity);

    @safety("TLS-SESSION-HANDSHAKE", "The session is live and native is writable")
    c_int32 r_std_tls_native_session_handshake(raw void*? session, raw c_int64* native);

    @safety("TLS-SESSION-READ",
            "The session is live, target addresses capacity writable bytes and the outputs are writable")
    c_int32 r_std_tls_native_session_read(raw void*? session,
                                          raw c_uint8* target,
                                          c_size capacity,
                                          raw c_size* count,
                                          raw c_int64* native);

    @safety("TLS-SESSION-WRITE", "The session is live, data addresses length bytes and native is writable")
    c_int32 r_std_tls_native_session_write(raw void*? session,
                                           raw const c_uint8*? data,
                                           c_size length,
                                           raw c_int64* native);

    @safety("TLS-SESSION-CLOSE", "The session is live and native is writable")
    c_int32 r_std_tls_native_session_close(raw void*? session, raw c_int64* native);

    @safety("TLS-SESSION-PROTOCOL", "The session is live and length is writable")
    raw const c_uint8*? r_std_tls_native_session_protocol(raw void*? session, raw c_size* length);

    @safety("TLS-SESSION-VERSION", "The session is live")
    c_int32 r_std_tls_native_session_version(raw void*? session);
}

/* The address of the first byte of a view as the provider reads it, null for an empty view. */
protected raw const c_uint8*? bytes_of(const u8[] data) {
    if (len(data) == 0usize) { return null; }
    unsafe {
        raw const u8* first = &data[0usize] as raw const u8*;
        raw const void* erased = first as raw const void*;
        return erased as raw const c_uint8*;
    }
}

/* The address of the first byte of a nonempty view as the provider writes it. */
protected raw c_uint8* target_of(u8[] data) {
    unsafe {
        raw u8* first = &data[0usize] as raw u8*;
        raw void* erased = first as raw void*;
        return erased as raw c_uint8*;
    }
}

/* R-SLIB-TLS-0002: why a configuration, a handshake or a session failed. */
@derive(format)
enum error_code {
    invalid_certificate,
    invalid_key,
    untrusted_certificate,
    name_mismatch,
    expired_certificate,
    handshake_failed,
    peer_alert,
    protocol_error,
    invalid_argument,
    frozen,
    closed,
    missing_authorities,
};

/* R-SLIB-TLS-0002: a failure of TLS; native_code is the error of Mbed TLS, zero when none. */
error tls_error { error_code code; i64 native_code; };

/* R-SLIB-TLS-0004: the protocol version that a handshake negotiated. */
@derive(format)
enum version { tls12, tls13 };

/* The status of one call of the native provider and the Mbed TLS error it reported. */
struct outcome { i32 status; i64 native; };

/* The status of the native provider as the error of the module; allocation failures and
   the success statuses are handled by the callers. */
protected error_code code_of(i32 status) {
    switch (status) {
    case -2: return error_code::invalid_certificate;
    case -3: return error_code::invalid_key;
    case -4: return error_code::untrusted_certificate;
    case -5: return error_code::name_mismatch;
    case -6: return error_code::expired_certificate;
    case -7: return error_code::handshake_failed;
    case -8: return error_code::peer_alert;
    case -9: return error_code::protocol_error;
    case -11: return error_code::frozen;
    case 2: return error_code::closed;
    default: return error_code::invalid_argument;
    }
}

/* Throws the failure that an outcome of the native provider reports. */
protected void check(outcome result) throws tls_error, std.alloc::alloc_error {
    if (result.status == 0) { return; }
    throw (result.status == -1) std.alloc::alloc_error::out_of_memory;
    throw tls_error {.code = code_of(result.status), .native_code = result.native};
}

/* R-SLIB-TLS-0003: the settings of a client or a server, shared by the sessions made from
   it; its first session freezes it. */
struct config { protected atomic raw void*? native; };

drop(config* self) {
    raw void*? handle = core::atomic_load(&self->native, core::memory_order::relaxed);
    unsafe { r_std_tls_native_config_release(handle); }
}

protected raw void*? config_handle(const config* settings) {
    return core::atomic_load(&settings->native, core::memory_order::relaxed);
}

protected config create_config(i32 server) throws std.alloc::alloc_error {
    unsafe {
        raw void*? handle = r_std_tls_native_config_create(server as c_int32);
        throw (handle == null) std.alloc::alloc_error::out_of_memory;
        return config {.native = handle};
    }
}

/* A client configuration verifies the certificate chain of its server. */
config client_config() throws std.alloc::alloc_error {
    return create_config(0);
}

/* A server configuration requires its identity and does not ask for client certificates. */
config server_config() throws std.alloc::alloc_error {
    return create_config(1);
}

protected outcome add_authority_native(raw void*? handle, const u8[] data) {
    c_int64 native = 0i64 as c_int64;
    unsafe {
        raw c_int64* native_out = &native as raw c_int64*;
        c_int32 status = r_std_tls_native_config_add_authority(
            handle, bytes_of(data), len(data) as c_size, native_out);
        return outcome {.status = status as i32, .native = native as i64};
    }
}

/* Trusts the certificates of a PEM text, or of one DER certificate. */
void config::add_authority(config* this, const u8[] certificates)
    throws tls_error, std.alloc::alloc_error {
    check(add_authority_native(config_handle(this), certificates));
}

protected outcome set_identity_native(raw void*? handle, const u8[] chain, const u8[] key) {
    c_int64 native = 0i64 as c_int64;
    unsafe {
        raw c_int64* native_out = &native as raw c_int64*;
        c_int32 status = r_std_tls_native_config_set_identity(handle,
                                                              bytes_of(chain),
                                                              len(chain) as c_size,
                                                              bytes_of(key),
                                                              len(key) as c_size,
                                                              native_out);
        return outcome {.status = status as i32, .native = native as i64};
    }
}

/* Sets the certificate chain of this side and its private key, PEM or DER. */
void config::set_identity(config* this, const u8[] chain, const std.secret::buffer* key)
    throws tls_error, std.alloc::alloc_error {
    check(set_identity_native(config_handle(this), chain, std.secret::as_slice(key)));
}

/* Offers an application protocol by ALPN, in the order of the calls. */
void config::add_protocol(config* this, str name) throws tls_error, std.alloc::alloc_error {
    const u8[] bytes = name;
    raw void*? handle = config_handle(this);
    unsafe {
        c_int32 status =
            r_std_tls_native_config_add_protocol(handle, bytes_of(bytes), len(bytes) as c_size);
        check(outcome {.status = status as i32, .native = 0i64});
    }
}

/* Requires, or with false skips, the verification of the certificate chain of the peer. */
void config::set_verification(config* this, bool required) throws tls_error, std.alloc::alloc_error {
    i32 flag = 0;
    if (required == true) { flag = 1; }
    raw void*? handle = config_handle(this);
    unsafe {
        c_int32 status = r_std_tls_native_config_set_verification(handle, flag as c_int32);
        check(outcome {.status = status as i32, .native = 0i64});
    }
}

/* ---- The certificate authorities of the system ---- */

/* The first index at or after `from` where `pattern` starts, or the length of `text`. */
protected usize find_from(const u8[] text, usize from, const u8[] pattern) {
    if (from >= len(text)) { return len(text); }
    switch (std.bytes::find_slice(text[from..len(text)], pattern)) {
    case variant o::some(found): return from + *found;
    case variant o::none: return len(text);
    }
}

/* Trusts each certificate of a PEM bundle on its own and returns how many it trusted. A block
   that does not parse is skipped: the bundles of some systems hold certificates that Mbed TLS
   does not read, and the others stay usable. */
protected usize trust_bundle(config* settings, const u8[] text) throws tls_error, std.alloc::alloc_error {
    str begin_text = "-----BEGIN CERTIFICATE-----";
    const u8[] begin = begin_text;
    str end_text = "-----END CERTIFICATE-----";
    const u8[] end_marker = end_text;
    usize trusted = 0usize;
    usize from = 0usize;
    while (from < len(text)) {
        usize start = find_from(text, from, begin);
        usize stop = find_from(text, start, end_marker);
        if (stop >= len(text)) { break; }
        usize after = stop + len(end_marker);
        try {
            settings->add_authority(text[start..after]);
            trusted += 1usize;
        } catch (tls_error rejected) {
            if (rejected.code != error_code::invalid_certificate) { throw rejected; }
        }
        from = after;
    }
    return trusted;
}

/* The bundle files of the system, tried in order when SSL_CERT_FILE is not set: the one macOS
   keeps, then those of common Linux distributions. */
protected str system_bundle(usize index) {
    switch (index) {
    case 0usize: return "/etc/ssl/cert.pem";
    case 1usize: return "/etc/ssl/certs/ca-certificates.crt";
    case 2usize: return "/etc/pki/tls/certs/ca-bundle.crt";
    case 3usize: return "/etc/pki/ca-trust/extracted/pem/tls-ca-bundle.pem";
    case 4usize: return "/etc/ssl/ca-bundle.pem";
    default: return "/etc/pki/tls/cacert.pem";
    }
}

/* The value of an environment variable, none when it is unset, empty or not readable. */
protected o<std.string::string> setting(str name) throws std.alloc::alloc_error {
    try {
        o<std.string::string> found = std.env::get(name);
        switch (move found) {
        case variant o::some(move value):
            const u8[] text = value;
            if (len(text) > 0usize) { return o::some(move value); }
            drop value;
        case variant o::none: break;
        }
    } catch (std.env::env_error rejected) {
        rejected as void;
    }
    return o::none;
}

/* The largest bundle the module reads. */
protected const usize bundle_limit = 16777216usize;

/* R-SLIB-TLS-0007: a client configuration that trusts the certificate authorities of the system:
   the PEM bundle named by SSL_CERT_FILE, or else the first bundle file of the system that can
   be read. */
async config system_client_config() throws tls_error, std.error::fault {
    config made = client_config();
    usize trusted = 0usize;
    o<std.string::string> chosen = setting("SSL_CERT_FILE");
    switch (move chosen) {
    case variant o::some(move name):
        std.fs::path path = std.fs::path_from_utf8(name);
        bytes content = await std.fs::read_file(&path, bundle_limit);
        trusted += trust_bundle(&made, content.as_slice());
    case variant o::none:
        bool found = false;
        for (usize index = 0usize; index < 6usize && found == false; index += 1usize) {
            try {
                std.fs::path path = std.fs::path_from_utf8(system_bundle(index));
                bytes content = await std.fs::read_file(&path, bundle_limit);
                trusted += trust_bundle(&made, content.as_slice());
                found = true;
            } catch (std.fs::fs_error missing) {
                missing as void;
            }
        }
    }
    throw (trusted == 0usize) tls_error {.code = error_code::missing_authorities, .native_code = 0i64};
    return move made;
}

/* R-SLIB-TLS-0004: a TLS session over a byte stream; it reads and writes application data and
   is itself a std.stream::Stream. One task reads it and one task writes it at a time. */
@generic<S: std.stream::Stream>
struct stream {
    S transport;
    protected atomic raw void*? native;
};

@generic<S: std.stream::Stream>
drop(stream<S>* self) {
    raw void*? handle = core::atomic_load(&self->native, core::memory_order::relaxed);
    unsafe { r_std_tls_native_session_release(handle); }
}

@generic<S: std.stream::Stream>
protected raw void*? stream<S>::session(const stream<S>* this) {
    return core::atomic_load(&this->native, core::memory_order::relaxed);
}

protected raw void*? create_session(const config* settings, str server_name)
    throws tls_error, std.alloc::alloc_error {
    const u8[] name = server_name;
    c_int32 status = 0i32 as c_int32;
    c_int64 native = 0i64 as c_int64;
    raw void*? handle = config_handle(settings);
    unsafe {
        raw c_int32* status_out = &status as raw c_int32*;
        raw c_int64* native_out = &native as raw c_int64*;
        raw void*? created = r_std_tls_native_session_create(
            handle, bytes_of(name), len(name) as c_size, status_out, native_out);
        check(outcome {.status = status as i32, .native = native as i64});
        return created;
    }
}

protected usize take_records(raw void*? handle, u8[] target) {
    unsafe {
        return r_std_tls_native_session_take(handle, target_of(target), len(target) as c_size)
            as usize;
    }
}

protected i32 feed_records(raw void*? handle, const u8[] data) {
    unsafe {
        return r_std_tls_native_session_feed(handle, bytes_of(data), len(data) as c_size) as i32;
    }
}

protected outcome handshake_step(raw void*? handle) {
    c_int64 native = 0i64 as c_int64;
    unsafe {
        raw c_int64* native_out = &native as raw c_int64*;
        c_int32 status = r_std_tls_native_session_handshake(handle, native_out);
        return outcome {.status = status as i32, .native = native as i64};
    }
}

/* Plaintext read from a session: an outcome and the count of bytes placed. */
struct reading { outcome result; usize count; };

protected reading read_plain(raw void*? handle, u8[] target) {
    c_int64 native = 0i64 as c_int64;
    c_size count = 0usize as c_size;
    unsafe {
        raw c_size* count_out = &count as raw c_size*;
        raw c_int64* native_out = &native as raw c_int64*;
        c_int32 status = r_std_tls_native_session_read(
            handle, target_of(target), len(target) as c_size, count_out, native_out);
        return reading {.result = outcome {.status = status as i32, .native = native as i64},
                        .count = count as usize};
    }
}

protected outcome write_plain(raw void*? handle, const u8[] data) {
    c_int64 native = 0i64 as c_int64;
    unsafe {
        raw c_int64* native_out = &native as raw c_int64*;
        c_int32 status =
            r_std_tls_native_session_write(handle, bytes_of(data), len(data) as c_size, native_out);
        return outcome {.status = status as i32, .native = native as i64};
    }
}

protected outcome close_session(raw void*? handle) {
    c_int64 native = 0i64 as c_int64;
    unsafe {
        raw c_int64* native_out = &native as raw c_int64*;
        c_int32 status = r_std_tls_native_session_close(handle, native_out);
        return outcome {.status = status as i32, .native = native as i64};
    }
}


protected i32 version_number(raw void*? handle) {
    unsafe { return r_std_tls_native_session_version(handle) as i32; }
}

/* Writes the records waiting for the peer to the transport. */
@generic<S: std.stream::Stream>
@scoped
protected async void stream<S>::send_pending(const stream<S>* this) throws std.error::fault {
    u8[4096] chunk = {};
    while (true) {
        usize count = take_records(this->session(), &chunk);
        if (count == 0usize) { return; }
        task_scope(1) io { await this->transport.write_all_from(chunk[0usize..count]); }
    }
}

/* Reads once from the transport into the session; false at the end of the transport. */
@generic<S: std.stream::Stream>
@scoped
protected async bool stream<S>::receive(const stream<S>* this) throws std.error::fault {
    u8[4096] buffer = {};
    usize count = 0usize;
    task_scope(1) io { count += await this->transport.read_into(&buffer); }
    if (count == 0usize) { return false; }
    throw (feed_records(this->session(), buffer[0usize..count]) != 0)
        std.alloc::alloc_error::out_of_memory;
    return true;
}

/* Runs the handshake to its end, sending the alert of a failure before reporting it. */
@generic<S: std.stream::Stream>
@scoped
protected async void stream<S>::handshake(const stream<S>* this) throws tls_error, std.error::fault {
    while (true) {
        outcome step = handshake_step(this->session());
        task_scope(1) io { await this->send_pending(); }
        if (step.status == 0) { return; }
        if (step.status == 1) {
            task_scope(1) io {
                bool more = await this->receive();
                throw (more == false)
                    tls_error {.code = error_code::handshake_failed, .native_code = 0i64};
            }
        } else {
            check(step);
        }
    }
}

/* R-SLIB-TLS-0004: starts a client session over the transport, verifying that the certificate
   of the server names server_name, and completes its handshake. */
@generic<S: std.stream::Stream & unborrowed>
@scoped
async stream<S> connect(S transport, const config* settings, str server_name)
    throws tls_error, std.error::fault {
    raw void*? handle = create_session(settings, server_name);
    stream<S> secured = stream<S> {.transport = move transport, .native = handle};
    task_scope(1) io { await secured.handshake(); }
    return move secured;
}

/* R-SLIB-TLS-0004: starts a server session over the transport and completes its handshake. */
@generic<S: std.stream::Stream & unborrowed>
@scoped
async stream<S> accept(S transport, const config* settings) throws tls_error, std.error::fault {
    raw void*? handle = create_session(settings, "");
    stream<S> secured = stream<S> {.transport = move transport, .native = handle};
    task_scope(1) io { await secured.handshake(); }
    return move secured;
}

/* The name that ALPN selected, a view of the protocol list of the configuration, which the
   session keeps alive and unchanged until the stream drops it: the view is anchored to the
   stream (Core R-UNSAFE-0008). */
@generic<S: std.stream::Stream>
protected const u8[] protocol_bytes(const stream<S>* secured) {
    c_size length = 0usize as c_size;
    raw void*? handle = secured->session();
    unsafe {
        raw c_size* length_out = &length as raw c_size*;
        raw const c_uint8*? name = r_std_tls_native_session_protocol(handle, length_out);
        raw const void*? erased = name as raw const void*?;
        return core::slice_from_raw_parts_in(secured, erased as raw const u8*?, length as usize);
    }
}

/* The application protocol that ALPN selected, empty when none was. */
@generic<S: std.stream::Stream>
str stream<S>::protocol(const stream<S>* this) {
    try {
        return core::validate_utf8(protocol_bytes(this));
    } catch (core::utf8_error failure) {
        /* The name is one of the configured texts, so it is UTF-8. */
        failure as void;
    }
    return "";
}

/* The protocol version of the session. */
@generic<S: std.stream::Stream>
version stream<S>::version(const stream<S>* this) {
    if (version_number(this->session()) == 12) { return version::tls12; }
    return version::tls13;
}

/* A failure of the session after its handshake: a failure of memory is an allocation error,
   and every other failure an I/O error of the stream with the error of Mbed TLS. */
protected void check_stream(outcome result) throws std.error::fault {
    throw (result.status == -1) std.alloc::alloc_error::out_of_memory;
    throw std.io::io_error {.code = std.io::error_code::other, .native_code = result.native};
}

@generic<S: std.stream::Stream>
impl std.stream::Reader for stream<S> {
    @scoped
    async usize read_into(const stream<S>* this, u8[] target) throws std.error::fault {
        if (len(target) == 0usize) { return 0usize; }
        while (true) {
            reading plain = read_plain(this->session(), target);
            if (plain.result.status == 2) { return 0usize; }
            if (plain.result.status == 0) { return plain.count; }
            if (plain.result.status != 1) { check_stream(plain.result); }
            task_scope(1) io {
                bool more = await this->receive();
                /* The transport ended without close_notify: the data may be truncated. */
                throw (more == false)
                    std.io::io_error {.code = std.io::error_code::broken_pipe, .native_code = 0i64};
            }
        }
    }
};

@generic<S: std.stream::Stream>
impl std.stream::Writer for stream<S> {
    @scoped
    async usize write_from(const stream<S>* this, const u8[] source) throws std.error::fault {
        usize length = len(source);
        if (length == 0usize) { return 0usize; }
        outcome written = write_plain(this->session(), source);
        if (written.status != 0) { check_stream(written); }
        task_scope(1) io { await this->send_pending(); }
        return length;
    }

    @scoped
    async void flush(const stream<S>* this) throws std.error::fault {
        task_scope(1) io {
            await this->send_pending();
            await this->transport.flush();
        }
    }

    /* Sends close_notify, then ends the write direction of the transport. */
    @scoped
    async void shutdown(const stream<S>* this) throws std.error::fault {
        outcome closed = close_session(this->session());
        if (closed.status != 0) { check_stream(closed); }
        task_scope(1) io {
            await this->send_pending();
            await this->transport.shutdown();
        }
    }
};

@generic<S: std.stream::Stream>
impl std.stream::Stream for stream<S> {};
