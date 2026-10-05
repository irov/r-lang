module std.postgres;
import std.bytes;
import std.hash;
import std.encoding;
import std.net;
import std.tls;
import std.stream;
import std.pool;
import std.time;
import std.uuid;

/* R-SLIB-PG-0001: a client of PostgreSQL over the frontend/backend protocol 3.0, written in R
   over std.net and std.tls. A connection is a handle of one session shared through an async
   mutex: every operation that talks to the server takes the session for its whole exchange and
   returns its task, so the operations of one connection take turns and a task borrows nothing
   from its caller (Core R-BORROW-0024). */

/* ---- Errors ---- */

/* R-SLIB-PG-0002: why an operation failed. */
@derive(format)
enum error_code {
    server,
    connection,
    protocol,
    authentication,
    unsupported,
    closed,
    invalid_value,
    column_range,
    null_value,
    missing_column,
    migration_mismatch,
};

/* R-SLIB-PG-0002: a failure; a server error carries its SQLSTATE, message, detail and hint. */
error pg_error {
    error_code code;
    std.string::string sqlstate;
    std.string::string message;
    std.string::string detail;
    std.string::string hint;
};

protected pg_error failure(error_code code, str message) throws std.alloc::alloc_error {
    return pg_error {.code = code, .sqlstate = std.string::create(), .message = std.string::from_str(message),
                     .detail = std.string::create(), .hint = std.string::create()};
}

/* ---- Values and rows ---- */

/* R-SLIB-PG-0003: a parameter or a column value. */
enum value {
    null_value,
    boolean(bool),
    integer(i64),
    real(f64),
    text(std.string::string),
    binary(bytes),
};

value value::of_text(str text) throws std.alloc::alloc_error {
    return value::text(std.string::from_str(text));
}

value value::of_bytes(const u8[] data) throws std.alloc::alloc_error {
    bytes copied = std.bytes::with_capacity(len(data));
    std.bytes::append(&copied, data);
    return value::binary(move copied);
}

/* R-SLIB-PG-0004: a column of a result: its name and the OID of its type. */
struct column { std.string::string name; u32 type_oid; };

/* R-SLIB-PG-0004: one row of a result, its values in the order of the columns. */
struct row { array<value> values; };

usize row::count(const row* this) {
    return len(this->values);
}

const value* row::at(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    if (index >= len(this->values)) {
        throw failure(error_code::column_range, "the row has no such column");
    }
    return &this->values[index];
}

bool row::is_null(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    switch (*this->at(index)) {
    case variant value::null_value: return true;
    default: return false;
    }
}

protected pg_error wrong_type(const value* found) throws std.alloc::alloc_error {
    switch (*found) {
    case variant value::null_value: return failure(error_code::null_value, "the column is NULL");
    default: return failure(error_code::invalid_value, "the column holds a value of another type");
    }
}

i64 row::integer(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    const value* found = this->at(index);
    switch (*found) {
    case variant value::integer(number): return *number;
    default: break;
    }
    throw wrong_type(found);
}

/* An integer is read as the nearest real. */
f64 row::real(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    const value* found = this->at(index);
    switch (*found) {
    case variant value::real(number): return *number;
    case variant value::integer(number): return *number as f64;
    default: break;
    }
    throw wrong_type(found);
}

bool row::boolean(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    const value* found = this->at(index);
    switch (*found) {
    case variant value::boolean(flag): return *flag;
    default: break;
    }
    throw wrong_type(found);
}

/* The text of a column of a type without its own value form, such as numeric, a date or JSON. */
str row::text(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    const value* found = this->at(index);
    switch (*found) {
    case variant value::text(text): return text->as_str();
    default: break;
    }
    throw wrong_type(found);
}

/* The bytes of a bytea column. */
const u8[] row::bytes(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    const value* found = this->at(index);
    switch (*found) {
    case variant value::binary(data): return std.array::as_slice(data);
    default: break;
    }
    throw wrong_type(found);
}

/* R-SLIB-PG-0004: the result of a statement: its columns, its rows and the number of rows that
   the statement reported in its command tag (rows changed, or rows returned by SELECT). */
struct rows { array<column> columns; array<row> items; u64 affected; };

protected rows empty_rows() {
    array<column> columns = [];
    array<row> items = [];
    return rows {.columns = move columns, .items = move items, .affected = 0u64};
}

/* R-SLIB-PG-0009: a notification of LISTEN/NOTIFY: the server process that sent it, its
   channel and its payload. */
struct notification { u32 process_id; std.string::string channel; std.string::string payload; };

/* ---- Messages of the protocol ---- */

/* A message of the server: its type byte and its body. */
protected struct message { u8 kind; bytes body; };

protected void put_u16(bytes* out, u16 number) throws std.alloc::alloc_error {
    std.bytes::append_u8(out, ((number as u32) >> 8u32) as u8);
    std.bytes::append_u8(out, ((number as u32) & 255u32) as u8);
}

protected void put_u32(bytes* out, u32 number) throws std.alloc::alloc_error {
    std.bytes::append_u8(out, (number >> 24u32) as u8);
    std.bytes::append_u8(out, ((number >> 16u32) & 255u32) as u8);
    std.bytes::append_u8(out, ((number >> 8u32) & 255u32) as u8);
    std.bytes::append_u8(out, (number & 255u32) as u8);
}

/* A string of the protocol: its bytes and a zero byte, which the text itself shall not hold. */
protected void put_string(bytes* out, str text) throws pg_error, std.alloc::alloc_error {
    const u8[] raw_text = text;
    for (usize index = 0usize; index < len(raw_text); index += 1usize) {
        throw (raw_text[index] == 0u8) failure(error_code::invalid_value, "a text holds a zero byte");
    }
    std.bytes::append(out, raw_text);
    std.bytes::append_u8(out, 0u8);
}

/* Starts a message of the given type; its length is filled in by finish_message. */
protected usize start_message(bytes* out, u8 kind) throws std.alloc::alloc_error {
    std.bytes::append_u8(out, kind);
    usize at = len(*out);
    put_u32(out, 0u32);
    return at;
}

protected void finish_message(bytes* out, usize at) throws pg_error, std.alloc::alloc_error {
    usize length = len(*out) - at;
    throw (length > 1073741823usize) failure(error_code::invalid_value, "a message exceeds 1 GiB");
    u32 encoded = length as u32;
    (*out)[at] = (encoded >> 24u32) as u8;
    (*out)[at + 1usize] = ((encoded >> 16u32) & 255u32) as u8;
    (*out)[at + 2usize] = ((encoded >> 8u32) & 255u32) as u8;
    (*out)[at + 3usize] = (encoded & 255u32) as u8;
}

/* The unsigned 32-bit number at index of a body, in network order. */
protected u32 get_u32(const u8[] body, usize index) throws pg_error, std.alloc::alloc_error {
    throw (index + 4usize > len(body)) failure(error_code::protocol, "a message of the server is too short");
    return ((body[index] as u32) << 24u32) | ((body[index + 1usize] as u32) << 16u32) |
           ((body[index + 2usize] as u32) << 8u32) | (body[index + 3usize] as u32);
}

protected u16 get_u16(const u8[] body, usize index) throws pg_error, std.alloc::alloc_error {
    throw (index + 2usize > len(body)) failure(error_code::protocol, "a message of the server is too short");
    return (((body[index] as u32) << 8u32) | (body[index + 1usize] as u32)) as u16;
}

/* The end of the string that starts at index: the index of its zero byte. */
protected usize string_end(const u8[] body, usize index) throws pg_error, std.alloc::alloc_error {
    for (usize at = index; at < len(body); at += 1usize) {
        if (body[at] == 0u8) { return at; }
    }
    throw failure(error_code::protocol, "a string of the server has no end");
}

protected std.string::string text_of(const u8[] data) throws pg_error, std.alloc::alloc_error {
    try {
        return std.string::from_utf8(data);
    } catch (std.string::string_error rejected) {
        rejected as void;
    }
    throw failure(error_code::protocol, "the server sent text that is not UTF-8");
}

/* The fields of an ErrorResponse or NoticeResponse as an error of the server. */
protected pg_error server_error(const u8[] body) throws pg_error, std.alloc::alloc_error {
    pg_error made = failure(error_code::server, "");
    usize at = 0usize;
    while (at < len(body) && body[at] != 0u8) {
        u8 field = body[at];
        usize end = string_end(body, at + 1usize);
        std.string::string text = text_of(body[at + 1usize..end]);
        switch (field) {
        case 67u8: made.sqlstate = move text;
        case 77u8: made.message = move text;
        case 68u8: made.detail = move text;
        case 72u8: made.hint = move text;
        default: drop text;
        }
        at = end + 1usize;
    }
    return move made;
}

/* ---- Values in the text format ---- */

/* Appends the bytes of a text. */
protected void put_text(bytes* out, str text) throws std.alloc::alloc_error {
    const u8[] raw_text = text;
    std.bytes::append(out, raw_text);
}

/* Whether two texts hold the same bytes. */
protected bool same(str left, str right) {
    const u8[] left_bytes = left;
    const u8[] right_bytes = right;
    return std.bytes::equal(left_bytes, right_bytes);
}

/* A real in the text format of the server, whose special values are NaN, Infinity and
   -Infinity. */
protected void put_real(bytes* out, f64 shown) throws std.alloc::alloc_error {
    if (std.math::is_nan_f64(shown) == true) {
        put_text(out, "NaN");
        return;
    }
    if (std.math::is_infinite_f64(shown) == true) {
        if (shown > 0.0) { put_text(out, "Infinity"); } else { put_text(out, "-Infinity"); }
        return;
    }
    std.string::string text = f"{shown}";
    std.bytes::append(out, text.as_bytes());
}

/* The text of a parameter; null_value is sent as NULL and has none. */
protected bytes parameter_text(const value* given) throws std.alloc::alloc_error {
    bytes out = {};
    switch (*given) {
    case variant value::null_value: break;
    case variant value::boolean(flag):
        if (*flag == true) { std.bytes::append_u8(&out, 116u8); } else { std.bytes::append_u8(&out, 102u8); }
    case variant value::integer(number):
        i64 shown = *number;
        std.string::string text = f"{shown}";
        std.bytes::append(&out, text.as_bytes());
    case variant value::real(number): put_real(&out, *number);
    case variant value::text(text): std.bytes::append(&out, text->as_bytes());
    case variant value::binary(data):
        /* The text form of bytea: \x and two hexadecimal digits per byte. */
        put_text(&out, "\\x");
        std.string::string hex = std.encoding::encode_hex(std.array::as_slice(data));
        std.bytes::append(&out, hex.as_bytes());
    }
    return move out;
}

protected bool is_null_parameter(const value* given) {
    switch (*given) {
    case variant value::null_value: return true;
    default: return false;
    }
}

/* A real in the text format of the server, whose special values are NaN, Infinity and
   -Infinity. */
protected f64 real_of(str text) throws pg_error, std.alloc::alloc_error {
    str spelled = text;
    if (same(text, "NaN") == true) { spelled = "nan"; }
    if (same(text, "Infinity") == true) { spelled = "inf"; }
    if (same(text, "-Infinity") == true) { spelled = "-inf"; }
    try {
        return std.convert::parse_f64(spelled);
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    throw failure(error_code::protocol, "the server sent a real that does not parse");
}

/* The value of a cell in the text format, by the type of its column: bool, the integer types
   and oid, float4 and float8, and bytea have their own forms, every other type is text. */
protected value value_of(u32 type_oid, const u8[] cell) throws pg_error, std.alloc::alloc_error {
    if (type_oid == 17u32) {
        throw (len(cell) < 2usize || cell[0usize] != 92u8 || cell[1usize] != 120u8)
            failure(error_code::protocol, "the server sent bytea that is not hexadecimal");
        std.string::string digits = text_of(cell[2usize..len(cell)]);
        try {
            return value::binary(std.encoding::decode_hex(digits.as_str()));
        } catch (std.convert::parse_error rejected) {
            rejected as void;
        }
        throw failure(error_code::protocol, "the server sent bytea that is not hexadecimal");
    }
    std.string::string text = text_of(cell);
    if (type_oid == 16u32) {
        return value::boolean(same(text.as_str(), "t"));
    }
    if (type_oid == 20u32 || type_oid == 21u32 || type_oid == 23u32 || type_oid == 26u32) {
        try {
            return value::integer(std.convert::parse_i64(text.as_str(), 10u32));
        } catch (std.convert::parse_error rejected) {
            rejected as void;
        }
        throw failure(error_code::protocol, "the server sent an integer that does not parse");
    }
    if (type_oid == 700u32 || type_oid == 701u32) {
        return value::real(real_of(text.as_str()));
    }
    return value::text(move text);
}

/* ---- The session ---- */

/* R-SLIB-PG-0005: how a connection is made. A nonempty socket names the Unix-domain socket file
   of the server, such as /tmp/.s.PGSQL.5432, and host and port are not used. */
struct options {
    std.string::string host = std.string::from_str("127.0.0.1");
    u16 port = 5432u16;
    std.string::string socket = std.string::create();
    std.string::string user = std.string::from_str("postgres");
    std.string::string password = std.string::create();
    std.string::string database = std.string::create();
    std.string::string application_name = std.string::create();
};

/* The value of an environment variable, none when it is not set or cannot be read. */
protected o<std.string::string> variable(str name) throws std.alloc::alloc_error {
    try {
        return std.env::get(name);
    } catch (std.env::env_error unreadable) {
        unreadable as void;
    }
    return o::none;
}

/* R-SLIB-PG-0005: options from the environment of the program, as libpq reads it: PGHOST (a
   directory of the Unix-domain socket when it starts with /), PGPORT, PGUSER, PGPASSWORD,
   PGDATABASE and PGAPPNAME; a variable that is not set keeps the default. */
options options::from_environment() throws pg_error, std.alloc::alloc_error {
    options made = options {};
    o<std.string::string> port = variable("PGPORT");
    switch (move port) {
    case variant o::some(move text):
        try {
            made.port = std.convert::parse_u16(text.as_str(), 10u32);
        } catch (std.convert::parse_error rejected) {
            rejected as void;
            throw failure(error_code::invalid_value, "PGPORT is not a port number");
        }
    case variant o::none: break;
    }
    o<std.string::string> host = variable("PGHOST");
    switch (move host) {
    case variant o::some(move text):
        const u8[] host_bytes = text.as_bytes();
        if (len(host_bytes) > 0usize && host_bytes[0usize] == 47u8) {
            str directory = text.as_str();
            u16 number = made.port;
            made.socket = f"{directory}/.s.PGSQL.{number}";
            drop text;
        } else {
            made.host = move text;
        }
    case variant o::none: break;
    }
    o<std.string::string> user = variable("PGUSER");
    switch (move user) {
    case variant o::some(move text): made.user = move text;
    case variant o::none: break;
    }
    o<std.string::string> password = variable("PGPASSWORD");
    switch (move password) {
    case variant o::some(move text): made.password = move text;
    case variant o::none: break;
    }
    o<std.string::string> database = variable("PGDATABASE");
    switch (move database) {
    case variant o::some(move text): made.database = move text;
    case variant o::none: break;
    }
    o<std.string::string> application = variable("PGAPPNAME");
    switch (move application) {
    case variant o::some(move text): made.application_name = move text;
    case variant o::none: break;
    }
    return move made;
}

/* R-SLIB-PG-0011: what cancel needs to ask the server to cancel the statement that a connection
   is running: the address of the server and the key of the session. */
struct canceller {
    protected std.string::string host;
    protected u16 port;
    protected std.string::string socket;
    protected u32 process_id;
    protected u32 secret_key;
};

/* The transport and the bytes received from it that are not yet handed out, the notifications
   received while waiting for other messages and the number of the next statement name. */
protected struct link {
    own dyn(std.stream::Stream)* stream;
    bytes received;
    usize parsed;
    bool broken;
    array<notification> pending;
    u64 next_name;
};

/* The state of a connection that its tasks share: the session under its lock, the transaction
   status of the last ReadyForQuery and the key for cancel. */
protected struct shared_state {
    std.async::mutex<link> session;
    atomic u32 status;
    canceller key;
};

/* R-SLIB-PG-0005: a connection to a database. */
struct connection { protected arc shared_state core; };

/* R-SLIB-PG-0008: the transaction status that the server reported last. */
enum transaction_status { idle, in_transaction, failed };

protected void ensure_usable(const link* conn) throws pg_error, std.alloc::alloc_error {
    throw (conn->broken == true) failure(error_code::closed, "the connection is closed");
}

/* Writes bytes to the server; a failure of the transport ends the session. */
@scoped
protected async void send(link* conn, const u8[] data) throws pg_error, std.error::fault {
    try {
        task_scope(1) io {
            await conn->stream.write_all_from(data);
            await conn->stream.flush();
        }
    } catch (std.error::fault broken) {
        conn->broken = true;
        throw broken;
    }
}

/* The complete message at the front of the received bytes, if one is there. */
protected o<message> take_message(link* conn) throws pg_error, std.alloc::alloc_error {
    usize available = len(conn->received) - conn->parsed;
    if (available < 5usize) { return o::none; }
    usize start = conn->parsed;
    u8 kind = conn->received[start];
    u32 length = get_u32(std.array::as_slice(&conn->received), start + 1usize);
    if (length < 4u32) {
        conn->broken = true;
        throw failure(error_code::protocol, "a message of the server has a wrong length");
    }
    usize total = 1usize + (length as usize);
    if (available < total) { return o::none; }
    bytes body = std.bytes::with_capacity(total - 5usize);
    std.bytes::append(&body, conn->received[start + 5usize..start + total]);
    conn->parsed = start + total;
    if (conn->parsed == len(conn->received)) {
        std.array::clear(&conn->received);
        conn->parsed = 0usize;
    }
    return o::some(message {.kind = kind, .body = move body});
}

/* The next message of the server, reading the transport when none is complete. */
@scoped
protected async message next_message(link* conn) throws pg_error, std.error::fault {
    u8[8192] chunk = {};
    while (true) {
        o<message> ready = take_message(conn);
        switch (move ready) {
        case variant o::some(move found): return move found;
        case variant o::none: break;
        }
        if (conn->parsed > 0usize) {
            bytes rest = {};
            std.bytes::append(&rest, conn->received[conn->parsed..len(conn->received)]);
            bytes old = core::replace(&conn->received, move rest);
            drop old;
            conn->parsed = 0usize;
        }
        usize count = 0usize;
        try {
            task_scope(1) io { count += await conn->stream.read_into(&chunk); }
        } catch (std.error::fault broken) {
            conn->broken = true;
            throw broken;
        }
        if (count == 0usize) {
            conn->broken = true;
            throw failure(error_code::closed, "the server closed the connection");
        }
        std.bytes::append(&conn->received, chunk[0usize..count]);
    }
}

/* Keeps a notification, skips a notice or a parameter status and refuses any other message
   that the server sends outside an exchange. */
protected void asynchronous(link* conn, const message* incoming) throws pg_error, std.alloc::alloc_error {
    if (incoming->kind == 65u8) {
        const u8[] body = incoming->body.as_slice();
        u32 process = get_u32(body, 0usize);
        usize channel_end = string_end(body, 4usize);
        usize payload_end = string_end(body, channel_end + 1usize);
        notification made = notification {.process_id = process,
                                           .channel = text_of(body[4usize..channel_end]),
                                           .payload = text_of(body[channel_end + 1usize..payload_end])};
        try {
            conn->pending.push(move made);
        } catch (std.array::push_error<notification> rejected) {
            switch (move rejected) {
            case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
            }
        }
        return;
    }
    if (incoming->kind == 78u8 || incoming->kind == 83u8) { return; }
    conn->broken = true;
    /* An error outside an exchange, such as the end of the server, ends the session. */
    throw (incoming->kind == 69u8) server_error(incoming->body.as_slice());
    throw failure(error_code::protocol, "the server sent an unexpected message");
}

/* ---- Startup and authentication ---- */

protected bool is_empty(const std.string::string* text) {
    const u8[] raw_text = text->as_bytes();
    return len(raw_text) == 0usize;
}

protected bytes startup_message(const options* settings) throws pg_error, std.alloc::alloc_error {
    bytes out = {};
    put_u32(&out, 0u32);
    put_u32(&out, 196608u32);
    put_string(&out, "user");
    put_string(&out, settings->user.as_str());
    if (is_empty(&settings->database) == false) {
        put_string(&out, "database");
        put_string(&out, settings->database.as_str());
    }
    if (is_empty(&settings->application_name) == false) {
        put_string(&out, "application_name");
        put_string(&out, settings->application_name.as_str());
    }
    put_string(&out, "client_encoding");
    put_string(&out, "UTF8");
    /* R-SLIB-PG-0013 reads timestamps and dates in the ISO style whatever the server default. */
    put_string(&out, "DateStyle");
    put_string(&out, "ISO, MDY");
    std.bytes::append_u8(&out, 0u8);
    finish_message(&out, 0usize);
    return move out;
}

protected bytes password_message(const u8[] secret) throws pg_error, std.alloc::alloc_error {
    bytes out = {};
    usize at = start_message(&out, 112u8);
    std.bytes::append(&out, secret);
    std.bytes::append_u8(&out, 0u8);
    finish_message(&out, at);
    return move out;
}

protected void put_hex(bytes* out, const u8[] data) throws std.alloc::alloc_error {
    std.string::string hex = std.encoding::encode_hex(data);
    std.bytes::append(out, hex.as_bytes());
}

/* The md5 method: "md5" and the hexadecimal MD5 of the hexadecimal MD5 of password and user,
   followed by the salt. */
protected bytes md5_answer(const options* settings, const u8[] salt) throws pg_error, std.alloc::alloc_error {
    bytes inner = {};
    std.bytes::append(&inner, settings->password.as_bytes());
    std.bytes::append(&inner, settings->user.as_bytes());
    std.hash::md5_digest first = std.hash::md5(inner.as_slice());
    bytes outer = {};
    put_hex(&outer, &first.bytes);
    std.bytes::append(&outer, salt);
    std.hash::md5_digest second = std.hash::md5(outer.as_slice());
    bytes answer = {};
    put_text(&answer, "md5");
    put_hex(&answer, &second.bytes);
    return password_message(answer.as_slice());
}

/* The state of a SCRAM-SHA-256 exchange (RFC 5802, RFC 7677) between its messages. */
protected struct scram {
    std.string::string nonce;
    std.string::string first_bare;
    bytes server_signature;
};

protected bytes hmac(const u8[] key, const u8[] data) throws std.alloc::alloc_error {
    std.hash::sha256_digest digest = std.hash::hmac_sha256(key, data);
    bytes out = std.bytes::with_capacity(32usize);
    std.bytes::append(&out, &digest.bytes);
    return move out;
}

protected bytes hmac_text(const u8[] key, str label) throws std.alloc::alloc_error {
    const u8[] label_bytes = label;
    return hmac(key, label_bytes);
}

protected bytes base64_of(str text) throws pg_error, std.alloc::alloc_error {
    try {
        return std.encoding::decode_base64(text);
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    throw failure(error_code::authentication, "the server sent malformed base64 in SCRAM");
}

protected u32 iterations_of(str text) throws pg_error, std.alloc::alloc_error {
    try {
        return std.convert::parse_u32(text, 10u32);
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    throw failure(error_code::authentication, "the server sent a malformed SCRAM iteration count");
}

/* Hi of RFC 5802: PBKDF2 with HMAC-SHA-256 and one block of output. */
protected bytes salted_password(const u8[] password, const u8[] salt, u32 iterations)
    throws std.alloc::alloc_error {
    bytes first = {};
    std.bytes::append(&first, salt);
    put_u32(&first, 1u32);
    std.hash::sha256_digest start = std.hash::hmac_sha256(password, first.as_slice());
    u8[32] result = start.bytes;
    u8[32] previous = start.bytes;
    for (u32 round = 1u32; round < iterations; round += 1u32) {
        std.hash::sha256_digest next = std.hash::hmac_sha256(password, &previous);
        previous = next.bytes;
        for (usize index = 0usize; index < 32usize; index += 1usize) {
            result[index] ^= previous[index];
        }
    }
    bytes out = std.bytes::with_capacity(32usize);
    std.bytes::append(&out, &result);
    return move out;
}

/* The SASLInitialResponse with the client-first-message of SCRAM-SHA-256: no channel binding and
   the user of the startup message. */
protected bytes scram_first(scram* state) throws pg_error, std.alloc::alloc_error {
    u8[18] random = {};
    std.random::fill(&random);
    state->nonce = std.encoding::encode_base64(&random);
    str nonce = state->nonce.as_str();
    state->first_bare = f"n=,r={nonce}";
    bytes data = {};
    put_text(&data, "n,,");
    std.bytes::append(&data, state->first_bare.as_bytes());
    bytes out = {};
    usize at = start_message(&out, 112u8);
    put_string(&out, "SCRAM-SHA-256");
    put_u32(&out, len(data) as u32);
    std.bytes::append(&out, data.as_slice());
    finish_message(&out, at);
    return move out;
}

/* The value of the attribute name= in a SCRAM message, or none. */
protected o<std.string::string> attribute(const u8[] text, u8 name) throws pg_error, std.alloc::alloc_error {
    usize at = 0usize;
    while (at + 1usize < len(text)) {
        usize end = at;
        while (end < len(text) && text[end] != 44u8) { end += 1usize; }
        if (text[at] == name && text[at + 1usize] == 61u8) {
            return o::some(text_of(text[at + 2usize..end]));
        }
        at = end + 1usize;
    }
    return o::none;
}

protected std.string::string required(o<std.string::string> found) throws pg_error, std.alloc::alloc_error {
    switch (move found) {
    case variant o::some(move text): return move text;
    case variant o::none: break;
    }
    throw failure(error_code::authentication, "the server sent an incomplete SCRAM message");
}

/* The SASLResponse with the client-final-message for the server-first-message. */
protected bytes scram_final(scram* state, const options* settings, const u8[] server_first)
    throws pg_error, std.alloc::alloc_error {
    std.string::string nonce = required(attribute(server_first, 114u8));
    std.string::string salt_text = required(attribute(server_first, 115u8));
    std.string::string count_text = required(attribute(server_first, 105u8));
    const u8[] ours = state->nonce.as_bytes();
    const u8[] theirs = nonce.as_bytes();
    throw (len(theirs) <= len(ours) || std.bytes::equal(theirs[0usize..len(ours)], ours) == false)
        failure(error_code::authentication, "the server changed the SCRAM nonce");
    bytes salt = base64_of(salt_text.as_str());
    u32 iterations = iterations_of(count_text.as_str());
    throw (iterations == 0u32) failure(error_code::authentication, "the server asked for no SCRAM iteration");
    bytes salted = salted_password(settings->password.as_bytes(), salt.as_slice(), iterations);
    bytes client_key = hmac_text(salted.as_slice(), "Client Key");
    std.hash::sha256_digest stored = std.hash::sha256(client_key.as_slice());
    str nonce_text = nonce.as_str();
    std.string::string without_proof = f"c=biws,r={nonce_text}";
    bytes auth = {};
    std.bytes::append(&auth, state->first_bare.as_bytes());
    put_text(&auth, ",");
    std.bytes::append(&auth, server_first);
    put_text(&auth, ",");
    std.bytes::append(&auth, without_proof.as_bytes());
    bytes signature = hmac(&stored.bytes, auth.as_slice());
    bytes proof = std.bytes::with_capacity(32usize);
    for (usize index = 0usize; index < len(signature); index += 1usize) {
        std.bytes::append_u8(&proof, (client_key[index] ^ signature[index]) as u8);
    }
    bytes server_key = hmac_text(salted.as_slice(), "Server Key");
    state->server_signature = hmac(server_key.as_slice(), auth.as_slice());
    std.string::string proof_text = std.encoding::encode_base64(proof.as_slice());
    bytes data = {};
    std.bytes::append(&data, without_proof.as_bytes());
    put_text(&data, ",p=");
    std.bytes::append(&data, proof_text.as_bytes());
    bytes out = {};
    usize at = start_message(&out, 112u8);
    std.bytes::append(&out, data.as_slice());
    finish_message(&out, at);
    return move out;
}

/* Checks the server-final-message: the server proves that it knows the password too. */
protected void scram_check(const scram* state, const u8[] server_final) throws pg_error, std.alloc::alloc_error {
    std.string::string verifier = required(attribute(server_final, 118u8));
    bytes expected = base64_of(verifier.as_str());
    throw (std.bytes::equal(expected.as_slice(), state->server_signature.as_slice()) == false)
        failure(error_code::authentication, "the server did not prove that it knows the password");
}

/* The answer to an Authentication message, empty when none is due. */
protected bytes answer_of(link* conn, const options* settings, scram* exchange, const u8[] body)
    throws pg_error, std.alloc::alloc_error {
    u32 code = get_u32(body, 0usize);
    switch (code) {
    case 0u32: break;
    case 3u32: return password_message(settings->password.as_bytes());
    case 5u32:
        throw (len(body) < 8usize) failure(error_code::protocol, "the server sent no MD5 salt");
        return md5_answer(settings, body[4usize..8usize]);
    case 10u32:
        bool offered = false;
        str mechanism_name = "SCRAM-SHA-256";
        const u8[] wanted = mechanism_name;
        usize at = 4usize;
        while (at < len(body) && body[at] != 0u8) {
            usize end = string_end(body, at);
            if (std.bytes::equal(body[at..end], wanted) == true) { offered = true; }
            at = end + 1usize;
        }
        throw (offered == false)
            failure(error_code::unsupported, "the server offers no SASL mechanism of this client");
        return scram_first(exchange);
    case 11u32: return scram_final(exchange, settings, body[4usize..len(body)]);
    case 12u32: scram_check(exchange, body[4usize..len(body)]);
    default:
        conn->broken = true;
        throw failure(error_code::unsupported, "the server asks for an authentication method this client lacks");
    }
    bytes none = {};
    return move none;
}

/* Runs the authentication exchange that follows the startup message, up to the first
   ReadyForQuery, keeping the key of the session; returns the transaction status. */
@scoped
protected async u32 authenticate(link* conn, const options* settings, canceller* key)
    throws pg_error, std.error::fault {
    scram exchange = scram {.nonce = std.string::create(), .first_bare = std.string::create(), .server_signature = {}};
    while (true) {
        task_scope(1) io {
            message incoming = await next_message(conn);
            switch (incoming.kind) {
            case 82u8:
                bytes answer = answer_of(conn, settings, &exchange, incoming.body.as_slice());
                if (len(answer) > 0usize) {
                    task_scope(1) writing { await send(conn, answer.as_slice()); }
                }
            case 75u8:
                key->process_id = get_u32(incoming.body.as_slice(), 0usize);
                key->secret_key = get_u32(incoming.body.as_slice(), 4usize);
            case 90u8:
                throw (len(incoming.body) < 1usize) failure(error_code::protocol, "ReadyForQuery without a status");
                return incoming.body[0usize] as u32;
            case 69u8:
                conn->broken = true;
                throw server_error(incoming.body.as_slice());
            default:
                asynchronous(conn, &incoming);
            }
        }
    }
    throw failure(error_code::protocol, "the authentication ended without ReadyForQuery");
}

/* Asks the server for TLS with the SSLRequest of the protocol; the server answers S or N. */
@scoped
protected async bool accepts_tls(const std.net::tcp_stream* tcp) throws std.error::fault {
    bytes request = {};
    put_u32(&request, 8u32);
    put_u32(&request, 80877103u32);
    u8[1] answer = {};
    usize count = 0usize;
    task_scope(1) io {
        await tcp->write_all_from(request.as_slice());
        count += await tcp->read_into(&answer);
    }
    return count == 1usize && answer[0usize] == 83u8;
}

/* Opens the transport: the Unix-domain socket, or TCP to host and port with TLS when a
   configuration is given. */
protected async own dyn(std.stream::Stream)* open_transport(std.string::string host, u16 port,
                                                            std.string::string socket,
                                                            o<arc std.tls::config> tls)
    throws pg_error, std.tls::tls_error, std.error::fault {
    bool secure = false;
    switch (tls) {
    case variant o::some(configured): configured as void; secure = true;
    case variant o::none: break;
    }
    if (is_empty(&socket) == false) {
        throw (secure == true) failure(error_code::connection, "TLS is for TCP connections");
        task_scope(1) local_io {
            std.net::unix_stream local = await std.net::unix_connect(socket.as_str(), o::none);
            return new std.net::unix_stream(move local);
        }
    }
    task_scope(1) io {
        std.net::tcp_stream tcp = await std.net::tcp_connect_name(host.as_str(), port);
        if (secure == false) { return new std.net::tcp_stream(move tcp); }
        u32 accepted = 0u32;
        task_scope(1) negotiation {
            if (await accepts_tls(&tcp) == true) { accepted += 1u32; }
        }
        throw (accepted == 0u32) failure(error_code::connection, "the server does not accept TLS");
        switch (tls) {
        case variant o::some(configured):
            std.tls::stream<std.net::tcp_stream> secured =
                await std.tls::connect(move tcp, &**configured, host.as_str());
            return new std.tls::stream<std.net::tcp_stream>(move secured);
        case variant o::none: break;
        }
    }
    throw failure(error_code::connection, "TLS needs a configuration");
}

protected std.string::string copy_of(const std.string::string* text) throws std.alloc::alloc_error {
    return std.string::from_str(text->as_str());
}

/* The transport, with a failure of TLS as a connection failure. */
protected async own dyn(std.stream::Stream)* opened(std.string::string host, u16 port,
                                                    std.string::string socket, o<arc std.tls::config> tls)
    throws pg_error, std.error::fault {
    try {
        return await open_transport(move host, port, move socket, move tls);
    } catch (std.tls::tls_error rejected) {
        std.tls::error_code code = rejected.code;
        str name = core::enum_name(code);
        std.string::string text = f"TLS failed: {name}";
        throw failure(error_code::connection, text.as_str());
    }
}

protected async connection connect_with(options settings, o<arc std.tls::config> tls)
    throws pg_error, std.error::fault {
    own dyn(std.stream::Stream)* stream = await opened(copy_of(&settings.host), settings.port,
                                                       copy_of(&settings.socket), move tls);
    array<notification> none = [];
    link conn = link {.stream = move stream, .received = {}, .parsed = 0usize, .broken = false,
                      .pending = move none, .next_name = 1u64};
    canceller key = canceller {.host = copy_of(&settings.host), .port = settings.port,
                               .socket = copy_of(&settings.socket), .process_id = 0u32, .secret_key = 0u32};
    bytes startup = startup_message(&settings);
    u32 status = 0u32;
    task_scope(1) opening {
        await send(&conn, startup.as_slice());
        status += await authenticate(&conn, &settings, &key);
    }
    return connection {.core = new arc shared_state {.session = std.async::mutex_new(move conn),
                                                    .status = status, .key = move key}};
}

/* R-SLIB-PG-0005: connects to the server, over TCP or a Unix-domain socket, and authenticates
   with the password of settings: cleartext, md5 or SCRAM-SHA-256, as the server asks. */
task<connection throws pg_error, std.error::fault> connect(options settings)
    throws std.async::start_error {
    o<arc std.tls::config> none = o::none;
    return connect_with(move settings, move none);
}

/* R-SLIB-PG-0005: connects over TCP and TLS with the configuration tls; a server that refuses
   TLS is a connection failure. */
task<connection throws pg_error, std.error::fault> connect_tls(options settings, arc std.tls::config tls)
    throws std.async::start_error {
    return connect_with(move settings, o::some(move tls));
}

/* ---- Statements ---- */

/* Parse, Bind, Describe, Execute and Sync of the extended protocol for one statement and its
   parameters in the text format; statement names a prepared statement, or is empty for the
   unnamed one that Parse prepares first. */
protected bytes extended_request(str statement, str sql, bool parse, const array<value>* parameters)
    throws pg_error, std.alloc::alloc_error {
    bytes out = {};
    if (parse == true) {
        usize at = start_message(&out, 80u8);
        put_string(&out, statement);
        put_string(&out, sql);
        put_u16(&out, 0u16);
        finish_message(&out, at);
    }
    throw (len(*parameters) > 65535usize) failure(error_code::invalid_value, "a statement takes at most 65535 parameters");
    usize bind = start_message(&out, 66u8);
    put_string(&out, "");
    put_string(&out, statement);
    put_u16(&out, 0u16);
    put_u16(&out, len(*parameters) as u16);
    const value[] listed = std.array::as_slice(parameters);
    for (usize index = 0usize; index < len(listed); index += 1usize) {
        if (is_null_parameter(&listed[index]) == true) {
            put_u32(&out, 4294967295u32);
        } else {
            bytes text = parameter_text(&listed[index]);
            put_u32(&out, len(text) as u32);
            std.bytes::append(&out, text.as_slice());
        }
    }
    put_u16(&out, 0u16);
    finish_message(&out, bind);
    usize describe = start_message(&out, 68u8);
    std.bytes::append_u8(&out, 80u8);
    put_string(&out, "");
    finish_message(&out, describe);
    usize execute = start_message(&out, 69u8);
    put_string(&out, "");
    put_u32(&out, 0u32);
    finish_message(&out, execute);
    usize sync = start_message(&out, 83u8);
    finish_message(&out, sync);
    return move out;
}

/* The columns of a RowDescription. */
protected array<column> columns_of(const u8[] body) throws pg_error, std.alloc::alloc_error {
    array<column> found = [];
    usize count = get_u16(body, 0usize) as usize;
    usize at = 2usize;
    for (usize index = 0usize; index < count; index += 1usize) {
        usize end = string_end(body, at);
        std.string::string name = text_of(body[at..end]);
        u32 type_oid = get_u32(body, end + 7usize);
        at = end + 19usize;
        try {
            found.push(column {.name = move name, .type_oid = type_oid});
        } catch (std.array::push_error<column> rejected) {
            switch (move rejected) {
            case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
            }
        }
    }
    return move found;
}

/* The row of a DataRow in the text format, decoded by the types of the columns. */
protected row row_of(const u8[] body, const array<column>* columns) throws pg_error, std.alloc::alloc_error {
    usize count = get_u16(body, 0usize) as usize;
    throw (count != len(*columns)) failure(error_code::protocol, "a row does not match its description");
    array<value> values = [];
    usize at = 2usize;
    for (usize index = 0usize; index < count; index += 1usize) {
        u32 size = get_u32(body, at);
        at += 4usize;
        value made = value::null_value;
        if (size != 4294967295u32) {
            throw (at + (size as usize) > len(body)) failure(error_code::protocol, "a row is shorter than its values");
            made = value_of((*columns)[index].type_oid, body[at..at + (size as usize)]);
            at += size as usize;
        }
        try {
            values.push(move made);
        } catch (std.array::push_error<value> rejected) {
            switch (move rejected) {
            case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
            }
        }
    }
    return row {.values = move values};
}

/* The number at the end of a command tag such as INSERT 0 3, SELECT 2 or UPDATE 1; zero when the
   tag ends without one. */
protected u64 affected_of(const u8[] body) {
    usize end = 0usize;
    while (end < len(body) && body[end] != 0u8) { end += 1usize; }
    usize start = end;
    while (start > 0usize && body[start - 1usize] >= 48u8 && body[start - 1usize] <= 57u8) { start -= 1usize; }
    u64 number = 0u64;
    for (usize index = start; index < end; index += 1usize) {
        number = number * 10u64 + ((body[index] - 48u8) as u64);
    }
    return number;
}

protected void push_row(array<row>* items, row made) throws std.alloc::alloc_error {
    try {
        items->push(move made);
    } catch (std.array::push_error<row> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Records the transaction status of the last ReadyForQuery. */
protected void note_status(const shared_state* core, u32 status) {
    core::atomic_store(&core->status, status, core::memory_order::release);
}

/* Reads the answers of an exchange up to ReadyForQuery: the columns and rows of the result and
   its command tag, and the transaction status that ReadyForQuery reports; an error of the server
   is thrown once the server is ready again. */
@scoped
protected async void collect(link* conn, const shared_state* core, rows* out, bool extended)
    throws pg_error, std.error::fault {
    o<pg_error> failed = o::none;
    while (true) {
        task_scope(1) io {
            message incoming = await next_message(conn);
            const u8[] body = incoming.body.as_slice();
            switch (incoming.kind) {
            case 84u8: out->columns = columns_of(body);
            case 68u8: push_row(&out->items, row_of(body, &out->columns));
            case 67u8: out->affected = affected_of(body);
            case 69u8:
                pg_error found = server_error(body);
                o<pg_error> earlier = core::replace(&failed, o::some(move found));
                drop earlier;
            case 90u8:
                throw (len(body) < 1usize) failure(error_code::protocol, "ReadyForQuery without a status");
                note_status(core, body[0usize] as u32);
                switch (move failed) {
                case variant o::some(move error_value): throw move error_value;
                case variant o::none: break;
                }
                return;
            case 71u8:
                /* COPY FROM STDIN outside copy_in: the client has no data, so the copy fails. In
                   copy-in mode the server ignored the Sync of an extended exchange, so another
                   one follows. */
                bytes refusal = {};
                usize at = start_message(&refusal, 102u8);
                put_string(&refusal, "COPY FROM STDIN needs copy_in");
                finish_message(&refusal, at);
                if (extended == true) {
                    usize sync = start_message(&refusal, 83u8);
                    finish_message(&refusal, sync);
                }
                task_scope(1) refusing { await send(conn, refusal.as_slice()); }
            case 49u8: break;
            case 50u8: break;
            case 51u8: break;
            case 110u8: break;
            case 116u8: break;
            case 73u8: break;
            case 115u8: break;
            case 72u8: break;
            case 100u8: break;
            case 99u8: break;
            default: asynchronous(conn, &incoming);
            }
        }
    }
    throw failure(error_code::protocol, "the exchange ended without ReadyForQuery");
}

/* One run of a statement with the extended protocol, under the session lock. */
protected async rows run_query(arc shared_state core, std.string::string sql, array<value> parameters,
                               std.string::string statement, bool parse)
    throws pg_error, std.error::fault {
    std.async::mutex_guard<link> guard = await core->session.lock();
    ensure_usable(guard.get());
    bytes request = extended_request(statement.as_str(), sql.as_str(), parse, &parameters);
    rows result = empty_rows();
    task_scope(1) io {
        await send(guard.get_mut(), request.as_slice());
        await collect(guard.get_mut(), &*core, &result, true);
    }
    return move result;
}

protected async u64 run_execute(arc shared_state core, std.string::string sql, array<value> parameters,
                                std.string::string statement, bool parse)
    throws pg_error, std.error::fault {
    rows result = await run_query(move core, move sql, move parameters, move statement, parse);
    return result.affected;
}

/* Statements of the simple query protocol, one or several separated by semicolons. */
protected async void run_script(arc shared_state core, std.string::string sql) throws pg_error, std.error::fault {
    std.async::mutex_guard<link> guard = await core->session.lock();
    ensure_usable(guard.get());
    bytes request = {};
    usize at = start_message(&request, 81u8);
    put_string(&request, sql.as_str());
    finish_message(&request, at);
    rows ignored = empty_rows();
    task_scope(1) io {
        await send(guard.get_mut(), request.as_slice());
        await collect(guard.get_mut(), &*core, &ignored, false);
    }
}

/* R-SLIB-PG-0006: runs one statement with the values of its parameters $1 to $N, in the text
   format, and keeps every row of its result. */
task<rows throws pg_error, std.error::fault> connection::query(const connection* this, str sql,
                                                              array<value> parameters)
    throws std.async::start_error, std.alloc::alloc_error {
    return run_query(std.arc::clone(&this->core), std.string::from_str(sql), move parameters,
                     std.string::create(), true);
}

/* R-SLIB-PG-0006: runs one statement and returns the number of rows that its command tag
   reports. */
task<u64 throws pg_error, std.error::fault> connection::execute(const connection* this, str sql,
                                                               array<value> parameters)
    throws std.async::start_error, std.alloc::alloc_error {
    return run_execute(std.arc::clone(&this->core), std.string::from_str(sql), move parameters,
                       std.string::create(), true);
}

/* R-SLIB-PG-0006: runs the statements of sql in order with the simple query protocol, without
   parameters; the first failure ends the script. */
task<void throws pg_error, std.error::fault> connection::execute_script(const connection* this, str sql)
    throws std.async::start_error, std.alloc::alloc_error {
    return run_script(std.arc::clone(&this->core), std.string::from_str(sql));
}

/* R-SLIB-PG-0008: transactions of the connection. */
task<void throws pg_error, std.error::fault> connection::begin(const connection* this)
    throws std.async::start_error, std.alloc::alloc_error {
    return this->execute_script("BEGIN");
}

task<void throws pg_error, std.error::fault> connection::commit(const connection* this)
    throws std.async::start_error, std.alloc::alloc_error {
    return this->execute_script("COMMIT");
}

task<void throws pg_error, std.error::fault> connection::rollback(const connection* this)
    throws std.async::start_error, std.alloc::alloc_error {
    return this->execute_script("ROLLBACK");
}

/* R-SLIB-PG-0008: the transaction status that the server reported at the end of the last
   exchange. */
transaction_status connection::transaction_status(const connection* this) {
    u32 status = core::atomic_load(&this->core->status, core::memory_order::acquire);
    if (status == 84u32) { return transaction_status::in_transaction; }
    if (status == 69u32) { return transaction_status::failed; }
    return transaction_status::idle;
}

/* R-SLIB-PG-0007: a statement prepared on a connection under a name of its own. */
struct statement { protected arc shared_state core; protected std.string::string name; };

protected async statement run_prepare(arc shared_state core, std.string::string sql) throws pg_error, std.error::fault {
    std.async::mutex_guard<link> guard = await core->session.lock();
    ensure_usable(guard.get());
    u64 number = (guard.get())->next_name;
    (guard.get_mut())->next_name += 1u64;
    std.string::string name = f"r_{number}";
    bytes request = {};
    usize parse = start_message(&request, 80u8);
    put_string(&request, name.as_str());
    put_string(&request, sql.as_str());
    put_u16(&request, 0u16);
    finish_message(&request, parse);
    usize sync = start_message(&request, 83u8);
    finish_message(&request, sync);
    rows ignored = empty_rows();
    task_scope(1) io {
        await send(guard.get_mut(), request.as_slice());
        await collect(guard.get_mut(), &*core, &ignored, true);
    }
    (move guard).unlock();
    return statement {.core = move core, .name = move name};
}

/* R-SLIB-PG-0007: prepares one statement on the server. */
task<statement throws pg_error, std.error::fault> connection::prepare(const connection* this, str sql)
    throws std.async::start_error, std.alloc::alloc_error {
    return run_prepare(std.arc::clone(&this->core), std.string::from_str(sql));
}

/* R-SLIB-PG-0007: runs a prepared statement with the values of its parameters. */
task<rows throws pg_error, std.error::fault> statement::query(const statement* this, array<value> parameters)
    throws std.async::start_error, std.alloc::alloc_error {
    return run_query(std.arc::clone(&this->core), std.string::create(), move parameters,
                     copy_of(&this->name), false);
}

task<u64 throws pg_error, std.error::fault> statement::execute(const statement* this, array<value> parameters)
    throws std.async::start_error, std.alloc::alloc_error {
    return run_execute(std.arc::clone(&this->core), std.string::create(), move parameters,
                       copy_of(&this->name), false);
}

protected async void run_close_statement(arc shared_state core, std.string::string name)
    throws pg_error, std.error::fault {
    std.async::mutex_guard<link> guard = await core->session.lock();
    ensure_usable(guard.get());
    bytes request = {};
    usize close = start_message(&request, 67u8);
    std.bytes::append_u8(&request, 83u8);
    put_string(&request, name.as_str());
    finish_message(&request, close);
    usize sync = start_message(&request, 83u8);
    finish_message(&request, sync);
    rows ignored = empty_rows();
    task_scope(1) io {
        await send(guard.get_mut(), request.as_slice());
        await collect(guard.get_mut(), &*core, &ignored, true);
    }
}

/* R-SLIB-PG-0007: closes the statement on the server; a statement dropped without close stays
   prepared until its connection ends. */
task<void throws pg_error, std.error::fault> statement::close(statement this)
    throws std.async::start_error {
    std.string::string none = std.string::create();
    std.string::string name = core::replace(&this.name, move none);
    arc shared_state core = std.arc::clone(&this.core);
    drop this;
    return run_close_statement(move core, move name);
}

/* ---- Notifications ---- */

/* An identifier in double quotes, with every double quote doubled. */
protected std.string::string quoted(str name) throws pg_error, std.alloc::alloc_error {
    const u8[] raw_name = name;
    bytes out = {};
    std.bytes::append_u8(&out, 34u8);
    for (usize index = 0usize; index < len(raw_name); index += 1usize) {
        throw (raw_name[index] == 0u8) failure(error_code::invalid_value, "a name holds a zero byte");
        if (raw_name[index] == 34u8) { std.bytes::append_u8(&out, 34u8); }
        std.bytes::append_u8(&out, raw_name[index]);
    }
    std.bytes::append_u8(&out, 34u8);
    return text_of(out.as_slice());
}

/* R-SLIB-PG-0009: LISTEN and UNLISTEN of a channel, and NOTIFY with a payload. */
task<void throws pg_error, std.error::fault> connection::listen(const connection* this, str channel)
    throws pg_error, std.async::start_error, std.alloc::alloc_error {
    std.string::string name = quoted(channel);
    str quoted_name = name.as_str();
    std.string::string sql = f"LISTEN {quoted_name}";
    return run_script(std.arc::clone(&this->core), move sql);
}

task<void throws pg_error, std.error::fault> connection::unlisten(const connection* this, str channel)
    throws pg_error, std.async::start_error, std.alloc::alloc_error {
    std.string::string name = quoted(channel);
    str quoted_name = name.as_str();
    std.string::string sql = f"UNLISTEN {quoted_name}";
    return run_script(std.arc::clone(&this->core), move sql);
}

task<u64 throws pg_error, std.error::fault> connection::notify(const connection* this, str channel, str payload)
    throws std.async::start_error, std.alloc::alloc_error {
    array<value> parameters = [];
    try {
        parameters.push(value::of_text(channel));
        parameters.push(value::of_text(payload));
    } catch (std.array::push_error<value> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload_error): throw payload_error.reason;
        }
    }
    return run_execute(std.arc::clone(&this->core), std.string::from_str("SELECT pg_notify($1, $2)"),
                       move parameters, std.string::create(), true);
}

protected async notification run_wait(arc shared_state core) throws pg_error, std.error::fault {
    std.async::mutex_guard<link> guard = await core->session.lock();
    ensure_usable(guard.get());
    while (len((guard.get())->pending) == 0usize) {
        task_scope(1) io {
            message incoming = await next_message(guard.get_mut());
            asynchronous(guard.get_mut(), &incoming);
        }
    }
    o<notification> first = std.array::remove(&(guard.get_mut())->pending, 0usize);
    switch (move first) {
    case variant o::some(move found): return move found;
    case variant o::none: break;
    }
    throw failure(error_code::protocol, "a notification went missing");
}

/* R-SLIB-PG-0009: the oldest notification of a channel this connection listens to, waiting
   for one when none has arrived; the deadline of an enclosing deadline block bounds the wait. */
task<notification throws pg_error, std.error::fault> connection::wait_notification(const connection* this)
    throws std.async::start_error {
    return run_wait(std.arc::clone(&this->core));
}

/* ---- COPY ---- */

protected bytes query_message(str sql) throws pg_error, std.alloc::alloc_error {
    bytes out = {};
    usize at = start_message(&out, 81u8);
    put_string(&out, sql);
    finish_message(&out, at);
    return move out;
}

/* The data of COPY FROM STDIN in CopyData messages of at most 64 KiB, then CopyDone. */
protected bytes copy_data(const u8[] data) throws pg_error, std.alloc::alloc_error {
    bytes out = {};
    usize at = 0usize;
    while (at < len(data)) {
        usize end = at + 65536usize;
        if (end > len(data)) { end = len(data); }
        usize start = start_message(&out, 100u8);
        std.bytes::append(&out, data[at..end]);
        finish_message(&out, start);
        at = end;
    }
    usize done = start_message(&out, 99u8);
    finish_message(&out, done);
    return move out;
}

/* Waits for the CopyInResponse or CopyOutResponse that answers a COPY; an error of the server
   is thrown once the server is ready again. */
@scoped
protected async void copy_started(link* conn, const shared_state* core, u8 expected)
    throws pg_error, std.error::fault {
    while (true) {
        task_scope(1) io {
            message incoming = await next_message(conn);
            if (incoming.kind == expected) { return; }
            if (incoming.kind == 69u8) {
                pg_error found = server_error(incoming.body.as_slice());
                rows ignored = empty_rows();
                task_scope(1) rest { await collect(conn, core, &ignored, false); }
                throw move found;
            }
            throw (incoming.kind == 71u8 || incoming.kind == 72u8)
                failure(error_code::protocol, "the statement is a COPY of the other direction");
            asynchronous(conn, &incoming);
        }
    }
}

protected async u64 run_copy_in(arc shared_state core, std.string::string sql, bytes data)
    throws pg_error, std.error::fault {
    std.async::mutex_guard<link> guard = await core->session.lock();
    ensure_usable(guard.get());
    bytes request = query_message(sql.as_str());
    bytes stream = copy_data(data.as_slice());
    rows result = empty_rows();
    task_scope(1) io {
        await send(guard.get_mut(), request.as_slice());
        await copy_started(guard.get_mut(), &*core, 71u8);
        await send(guard.get_mut(), stream.as_slice());
        await collect(guard.get_mut(), &*core, &result, false);
    }
    return result.affected;
}

/* The CopyData messages of COPY TO STDOUT, up to CopyDone. */
@scoped
protected async void copy_rows(link* conn, bytes* out) throws pg_error, std.error::fault {
    while (true) {
        task_scope(1) io {
            message incoming = await next_message(conn);
            if (incoming.kind == 99u8) { return; }
            if (incoming.kind == 100u8) {
                std.bytes::append(out, incoming.body.as_slice());
            } else {
                throw (incoming.kind == 69u8) server_error(incoming.body.as_slice());
                asynchronous(conn, &incoming);
            }
        }
    }
}

protected async bytes run_copy_out(arc shared_state core, std.string::string sql) throws pg_error, std.error::fault {
    std.async::mutex_guard<link> guard = await core->session.lock();
    ensure_usable(guard.get());
    bytes request = query_message(sql.as_str());
    bytes data = {};
    rows result = empty_rows();
    task_scope(1) io {
        await send(guard.get_mut(), request.as_slice());
        await copy_started(guard.get_mut(), &*core, 72u8);
        await copy_rows(guard.get_mut(), &data);
        await collect(guard.get_mut(), &*core, &result, false);
    }
    return move data;
}

/* R-SLIB-PG-0010: COPY ... FROM STDIN with data in the format the statement names; returns the
   number of rows copied. */
task<u64 throws pg_error, std.error::fault> connection::copy_in(const connection* this, str sql, bytes data)
    throws std.async::start_error, std.alloc::alloc_error {
    return run_copy_in(std.arc::clone(&this->core), std.string::from_str(sql), move data);
}

/* R-SLIB-PG-0010: COPY ... TO STDOUT; returns the data. */
task<bytes throws pg_error, std.error::fault> connection::copy_out(const connection* this, str sql)
    throws std.async::start_error, std.alloc::alloc_error {
    return run_copy_out(std.arc::clone(&this->core), std.string::from_str(sql));
}

/* ---- Cancel and close ---- */

/* R-SLIB-PG-0011: what cancel needs for the statement that the connection runs. */
canceller connection::canceller(const connection* this) throws std.alloc::alloc_error {
    const canceller* key = &this->core->key;
    return canceller {.host = copy_of(&key->host), .port = key->port, .socket = copy_of(&key->socket),
                      .process_id = key->process_id, .secret_key = key->secret_key};
}

protected async void run_cancel(canceller target) throws pg_error, std.error::fault {
    o<arc std.tls::config> none = o::none;
    own dyn(std.stream::Stream)* stream =
        await opened(copy_of(&target.host), target.port, copy_of(&target.socket), move none);
    bytes request = {};
    put_u32(&request, 16u32);
    put_u32(&request, 80877102u32);
    put_u32(&request, target.process_id);
    put_u32(&request, target.secret_key);
    /* The server closes this connection as soon as it has read the request, so the stream is
       only dropped: a shutdown could meet a connection that the server has already reset. */
    task_scope(1) io {
        await stream.write_all_from(request.as_slice());
    }
}

/* R-SLIB-PG-0011: asks the server, over a connection of its own, to cancel the statement that
   the connection of target runs; the statement then fails with SQLSTATE 57014. A request that
   arrives when no statement runs does nothing. */
task<void throws pg_error, std.error::fault> cancel(canceller target) throws std.async::start_error {
    return run_cancel(move target);
}

protected async void run_close(arc shared_state core) throws pg_error, std.error::fault {
    std.async::mutex_guard<link> guard = await core->session.lock();
    if ((guard.get())->broken == true) { return; }
    bytes request = {};
    usize at = start_message(&request, 88u8);
    finish_message(&request, at);
    (guard.get_mut())->broken = true;
    /* The server ends the session as soon as it reads Terminate and may reset the connection
       before the shutdown: the session is over either way, so the transport's end is no failure. */
    try {
        task_scope(1) io {
            await (guard.get())->stream.write_all_from(request.as_slice());
            await (guard.get())->stream.shutdown();
        }
    } catch (std.error::fault ended) {
        ended as void;
    }
}

/* R-SLIB-PG-0005: ends the session with Terminate; operations of other handles of the connection
   then fail with closed. */
task<void throws pg_error, std.error::fault> connection::close(connection this) throws std.async::start_error {
    arc shared_state core = std.arc::clone(&this.core);
    drop this;
    return run_close(move core);
}

/* R-SLIB-PG-0005: another handle of the same connection, for another task. */
connection connection::share(const connection* this) {
    return connection {.core = std.arc::clone(&this->core)};
}

/* ---- Pools ---- */

protected options copy_options(const options* settings) throws std.alloc::alloc_error {
    return options {.host = copy_of(&settings->host), .port = settings->port, .socket = copy_of(&settings->socket),
                    .user = copy_of(&settings->user), .password = copy_of(&settings->password),
                    .database = copy_of(&settings->database),
                    .application_name = copy_of(&settings->application_name)};
}

protected async std.pool::pool<connection> run_pool(options settings, usize size) throws pg_error, std.error::fault {
    array<connection> made = [];
    for (usize index = 0usize; index < size; index += 1usize) {
        connection opened_connection = await connect(copy_options(&settings));
        try {
            made.push(move opened_connection);
        } catch (std.array::push_error<connection> rejected) {
            switch (move rejected) {
            case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
            }
        }
    }
    return std.pool::pool<connection>::create(move made);
}

/* R-SLIB-PG-0012: a pool of size connections made with settings; a lease of the pool lends one
   of them to a task (R-SLIB-POOL-0001). */
task<std.pool::pool<connection> throws pg_error, std.error::fault> connect_pool(options settings, usize size)
    throws std.async::start_error {
    return run_pool(move settings, size);
}

/* ---- Typed cells and parameters (R-SLIB-PG-0013, R-SLIB-PG-0014) ---- */

protected void push_value(array<value>* items, value made) throws std.alloc::alloc_error {
    try {
        items->push(move made);
    } catch (std.array::push_error<value> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}


/* The value of 1 to `count` decimal digits at `at`, or -1. */
protected i64 digits_at(const u8[] text, usize at, usize count) {
    if (at + count > len(text)) { return -1i64; }
    i64 number = 0i64;
    for (usize index = 0usize; index < count; index += 1usize) {
        u8 c = text[at + index];
        if (c < 48u8 || c > 57u8) { return -1i64; }
        number = number * 10i64 + (c as i64 - 48i64);
    }
    return number;
}

/* The instant of the ISO text of a timestamp with or without a time zone, as the server writes
   it with DateStyle ISO: YYYY-MM-DD HH:MM:SS[.ffffff][+HH[:MM[:SS]]]. A text without an offset
   is read as UTC. */
protected std.time::system_time time_of(const u8[] text) throws pg_error, std.alloc::alloc_error {
    i64 year = digits_at(text, 0usize, 4usize);
    i64 month = digits_at(text, 5usize, 2usize);
    i64 day = digits_at(text, 8usize, 2usize);
    i64 hour = digits_at(text, 11usize, 2usize);
    i64 minute = digits_at(text, 14usize, 2usize);
    i64 second = digits_at(text, 17usize, 2usize);
    throw (year < 0i64 || month < 0i64 || day < 0i64 || hour < 0i64 || minute < 0i64 || second < 0i64 ||
           text[4usize] != 45u8 || text[7usize] != 45u8 || (text[10usize] != 32u8 && text[10usize] != 84u8) ||
           text[13usize] != 58u8 || text[16usize] != 58u8)
        failure(error_code::invalid_value, "the column is not a timestamp of the ISO style");
    usize at = 19usize;
    u32 nanoseconds = 0u32;
    if (at < len(text) && text[at] == 46u8) {
        at += 1usize;
        u32 scale = 100000000u32;
        while (at < len(text) && text[at] >= 48u8 && text[at] <= 57u8) {
            nanoseconds += (text[at] as u32 - 48u32) * scale;
            scale /= 10u32;
            at += 1usize;
        }
    }
    i64 offset = 0i64;
    if (at < len(text) && (text[at] == 43u8 || text[at] == 45u8)) {
        i64 sign = 1i64;
        if (text[at] == 45u8) { sign = -1i64; }
        i64 hours = digits_at(text, at + 1usize, 2usize);
        throw (hours < 0i64) failure(error_code::invalid_value, "the offset of the timestamp is not valid");
        offset = hours * 3600i64;
        at += 3usize;
        if (at < len(text) && text[at] == 58u8) {
            offset += digits_at(text, at + 1usize, 2usize) * 60i64;
            at += 3usize;
            if (at < len(text) && text[at] == 58u8) {
                offset += digits_at(text, at + 1usize, 2usize);
                at += 3usize;
            }
        }
        offset *= sign;
    }
    throw (at != len(text)) failure(error_code::invalid_value, "the column is not a timestamp of the ISO style");
    try {
        std.time::system_time local = std.time::from_utc(std.time::utc_datetime {
            .year = year as i32, .month = month as u8, .day = day as u8, .hour = hour as u8, .minute = minute as u8,
            .second = second as u8, .nanosecond = nanoseconds});
        return std.time::system_time {.unix_seconds = local.unix_seconds - offset, .nanoseconds = nanoseconds};
    } catch (std.time::time_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_value, "the timestamp is outside the calendar");
}

/* R-SLIB-PG-0013: the instant of a timestamptz column, or of a timestamp column read as UTC. */
std.time::system_time row::time(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    str text = this->text(index);
    return time_of(text);
}

/* R-SLIB-PG-0013: the date of a date column at midnight. */
std.time::utc_datetime row::date(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    str text = this->text(index);
    const u8[] bytes_of_text = text;
    i64 year = digits_at(bytes_of_text, 0usize, 4usize);
    i64 month = digits_at(bytes_of_text, 5usize, 2usize);
    i64 day = digits_at(bytes_of_text, 8usize, 2usize);
    throw (len(bytes_of_text) != 10usize || year < 0i64 || month < 1i64 || day < 1i64)
        failure(error_code::invalid_value, "the column is not a date of the ISO style");
    return std.time::utc_datetime {.year = year as i32, .month = month as u8, .day = day as u8, .hour = 0u8,
                                   .minute = 0u8, .second = 0u8, .nanosecond = 0u32};
}

/* R-SLIB-PG-0013: the identifier of a uuid column. */
std.uuid::uuid row::uuid(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    str text = this->text(index);
    try {
        return std.uuid::parse(text);
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_value, "the column is not a uuid");
}

/* R-SLIB-PG-0013: the document of a json or jsonb column. */
std.json::value row::json(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    str text = this->text(index);
    const u8[] bytes_of_text = text;
    try {
        return std.json::parse(bytes_of_text);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_value, "the column is not JSON");
}

/* The text of bytes that a statement or a cell holds; they are UTF-8 unless the server sent
   something else. */
protected std.string::string text_of_bytes(const u8[] data) throws pg_error, std.alloc::alloc_error {
    try {
        return std.string::from_utf8(data);
    } catch (std.string::string_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_value, "a text is not UTF-8");
}

/* The elements of a one-dimensional array in the text form of the server, NULL as none. The
   bytes of an element are copied as they are, so text in any script keeps its UTF-8. */
protected array<o<std.string::string>> array_elements(const u8[] text) throws pg_error, std.alloc::alloc_error {
    array<o<std.string::string>> items = [];
    usize at = 0usize;
    /* A lower bound written as [1:2]={...} is skipped. */
    if (at < len(text) && text[at] == 91u8) {
        while (at < len(text) && text[at] != 61u8) { at += 1usize; }
        at += 1usize;
    }
    throw (at >= len(text) || text[at] != 123u8 || text[len(text) - 1usize] != 125u8)
        failure(error_code::invalid_value, "the column is not an array");
    at += 1usize;
    usize stop = len(text) - 1usize;
    if (at == stop) { return move items; }
    while (at <= stop) {
        bytes element = {};
        bool in_quotes = false;
        if (text[at] == 123u8) { throw failure(error_code::unsupported, "the array has more than one dimension"); }
        if (text[at] == 34u8) {
            in_quotes = true;
            at += 1usize;
            while (at < stop && text[at] != 34u8) {
                if (text[at] == 92u8) { at += 1usize; }
                throw (at >= stop) failure(error_code::invalid_value, "the array is not complete");
                std.bytes::append_u8(&element, text[at]);
                at += 1usize;
            }
            throw (at >= stop) failure(error_code::invalid_value, "the array is not complete");
            at += 1usize;
        } else {
            while (at < stop && text[at] != 44u8) {
                std.bytes::append_u8(&element, text[at]);
                at += 1usize;
            }
        }
        const u8[] written = element.as_slice();
        usize written_length = len(written);
        bool null_element = in_quotes == false && written_length == 4usize && (written[0usize] == 78u8 || written[0usize] == 110u8) &&
                            (written[1usize] == 85u8 || written[1usize] == 117u8) && (written[2usize] == 76u8 || written[2usize] == 108u8) &&
                            (written[3usize] == 76u8 || written[3usize] == 108u8);
        try {
            if (null_element == true) {
                items.push(o::none);
            } else {
                items.push(o::some(text_of_bytes(written)));
            }
        } catch (std.array::push_error<o<std.string::string>> rejected) {
            switch (move rejected) {
            case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
            }
        }
        drop element;
        if (at < stop) {
            throw (text[at] != 44u8) failure(error_code::invalid_value, "the array is not well formed");
        }
        at += 1usize;
    }
    return move items;
}

/* R-SLIB-PG-0013: the elements of a one-dimensional array column as text, NULL as none. */
array<o<std.string::string>> row::text_array(const row* this, usize index) throws pg_error, std.alloc::alloc_error {
    str text = this->text(index);
    return array_elements(text);
}

/* R-SLIB-PG-0014: the parameter of an instant, written in RFC 3339 with microseconds. */
value value::of_time(std.time::system_time at) throws pg_error, std.alloc::alloc_error {
    try {
        std.string::string text = std.time::format_rfc3339(at, 6u32);
        return value::text(move text);
    } catch (std.time::time_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_value, "the instant has no RFC 3339 form");
}

/* R-SLIB-PG-0014: the parameter of a uuid. */
value value::of_uuid(const std.uuid::uuid* id) throws std.alloc::alloc_error {
    std.uuid::uuid shown = *id;
    std.string::string text = f"{shown}";
    return value::text(move text);
}

/* R-SLIB-PG-0014: the parameter of a JSON document for a json or jsonb column. */
value value::of_json(const std.json::value* document) throws pg_error, std.alloc::alloc_error {
    try {
        return value::text(std.json::stringify(document));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_value, "the document cannot be written");
}

/* Appends one element of an array literal: NULL, or a quoted text with its quotes and
   backslashes escaped. */
protected void put_element(bytes* out, const value* item) throws std.alloc::alloc_error {
    switch (*item) {
    case variant value::null_value: put_text(out, "NULL");
    default:
        bytes text = parameter_text(item);
        std.bytes::append_u8(out, 34u8);
        const u8[] raw_text = text.as_slice();
        for (usize index = 0usize; index < len(raw_text); index += 1usize) {
            if (raw_text[index] == 34u8 || raw_text[index] == 92u8) { std.bytes::append_u8(out, 92u8); }
            std.bytes::append_u8(out, raw_text[index]);
        }
        std.bytes::append_u8(out, 34u8);
    }
}

/* R-SLIB-PG-0014: the parameter of a one-dimensional array of values, in the text form of the
   server; the server casts it to the type of its place. */
value value::of_array(const array<value>* items) throws pg_error, std.alloc::alloc_error {
    bytes literal = {};
    std.bytes::append_u8(&literal, 123u8);
    for (usize index = 0usize; index < len(*items); index += 1usize) {
        if (index > 0usize) { std.bytes::append_u8(&literal, 44u8); }
        put_element(&literal, &(*items)[index]);
    }
    std.bytes::append_u8(&literal, 125u8);
    return value::text(text_of_bytes(literal.as_slice()));
}

/* ---- Rows and structs (R-SLIB-PG-0015) ---- */

/* R-SLIB-PG-0015: the index of the first column of a name. */
usize rows::column_index(const rows* this, str name) throws pg_error, std.alloc::alloc_error {
    const u8[] wanted = name;
    for (usize index = 0usize; index < len(this->columns); index += 1usize) {
        if (std.bytes::equal(this->columns[index].name.as_bytes(), wanted) == true) { return index; }
    }
    pg_error missing = failure(error_code::missing_column, "the result has no column of that name");
    missing.detail.append(name);
    throw move missing;
}

protected bool schema_is(const std.json::value* described, str expected) {
    switch (std.json::find(described, "type")) {
    case variant o::some(found):
        if (std.json::kind(*found) != std.json::value_kind::string) { return false; }
        str text = std.json::text(*found);
        return same(text, expected);
    case variant o::none: return false;
    }
}

/* The alternative of a nullable property that is not null, or the property itself. */
protected const std.json::value* non_null(const std.json::value* property) {
    switch (std.json::find(property, "anyOf")) {
    case variant o::some(choices):
        for (usize index = 0usize; index < std.json::len(*choices); index += 1usize) {
            switch (std.json::get(*choices, index)) {
            case variant o::some(choice):
                if (schema_is(*choice, "null") == false) { return *choice; }
            case variant o::none: break;
            }
        }
        return property;
    case variant o::none: return property;
    }
}

protected bool accepts_null(const std.json::value* property) {
    switch (std.json::find(property, "anyOf")) {
    case variant o::some(choices):
        for (usize index = 0usize; index < std.json::len(*choices); index += 1usize) {
            switch (std.json::get(*choices, index)) {
            case variant o::some(choice):
                if (schema_is(*choice, "null") == true) { return true; }
            case variant o::none: break;
            }
        }
        return false;
    case variant o::none: return false;
    }
}

protected bool required_property(const std.json::value* described, str key) {
    switch (std.json::find(described, "required")) {
    case variant o::some(names):
        for (usize index = 0usize; index < std.json::len(*names); index += 1usize) {
            switch (std.json::get(*names, index)) {
            case variant o::some(found):
                str text = std.json::text(*found);
                if (same(text, key) == true) { return true; }
            case variant o::none: break;
            }
        }
        return false;
    case variant o::none: return false;
    }
}

/* The OIDs of the timestamp, timestamptz, json, jsonb and array types whose cells convert by
   their form. */
protected bool is_time_oid(u32 oid) { return oid == 1114u32 || oid == 1184u32; }

protected bool is_json_oid(u32 oid) { return oid == 114u32 || oid == 3802u32; }

protected bool is_array_oid(u32 oid) {
    return oid == 1000u32 || oid == 1001u32 || oid == 1005u32 || oid == 1007u32 || oid == 1009u32 || oid == 1015u32 ||
           oid == 1016u32 || oid == 1021u32 || oid == 1022u32 || oid == 1115u32 || oid == 1182u32 || oid == 1185u32 ||
           oid == 1231u32 || oid == 2951u32 || oid == 199u32 || oid == 3807u32;
}

protected std.json::value json_number(str text) throws pg_error, std.alloc::alloc_error {
    const u8[] digits = text;
    try {
        std.json::number number = std.json::parse_number(digits);
        return std.json::from_number(&number);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_value, "a value is not a JSON number");
}

/* The JSON value of a text cell for the schema of its field: a number or boolean of its text
   when the field is one, else a string. */
protected std.json::value text_for(const std.json::value* described, str text) throws pg_error, std.alloc::alloc_error {
    try {
        if (schema_is(described, "integer") == true || schema_is(described, "number") == true) { return json_number(text); }
        if (schema_is(described, "boolean") == true) {
            if (same(text, "t") == true || same(text, "true") == true) { return std.json::from_bool(true); }
            if (same(text, "f") == true || same(text, "false") == true) { return std.json::from_bool(false); }
            throw failure(error_code::invalid_value, "a value is not a boolean");
        }
        const u8[] raw_text = text;
        return std.json::from_string(raw_text);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_value, "a value cannot be written as JSON");
}

/* The JSON value of a cell for its column type and the schema of its field. */
protected std.json::value cell_json(const value* cell, u32 oid, const std.json::value* property)
    throws pg_error, std.alloc::alloc_error {
    try {
        switch (*cell) {
        case variant value::null_value: return std.json::null();
        case variant value::boolean(flag): return std.json::from_bool(*flag);
        case variant value::integer(number):
            i64 shown = *number;
            std.string::string text = f"{shown}";
            if (schema_is(non_null(property), "string") == true) { return std.json::from_string(text.as_bytes()); }
            return json_number(text.as_str());
        case variant value::real(number):
            f64 shown = *number;
            throw (shown != shown || shown - shown != 0.0) failure(error_code::invalid_value, "a real is not finite");
            std.string::string text = f"{shown}";
            return json_number(text.as_str());
        case variant value::binary(data):
            if (schema_is(non_null(property), "string") == true) {
                std.string::string encoded = std.encoding::encode_base64(std.array::as_slice(data));
                return std.json::from_string(encoded.as_bytes());
            }
            std.json::value items = std.json::array();
            const u8[] raw_data = std.array::as_slice(data);
            for (usize index = 0usize; index < len(raw_data); index += 1usize) {
                u8 byte = raw_data[index];
                std.string::string text = f"{byte}";
                std.json::append(&items, json_number(text.as_str()));
            }
            return move items;
        case variant value::text(text):
            const std.json::value* described = non_null(property);
            if (is_time_oid(oid) == true) {
                std.time::system_time at = time_of(text->as_bytes());
                if (schema_is(described, "integer") == true) {
                    std.string::string seconds = f"{at.unix_seconds}";
                    return json_number(seconds.as_str());
                }
                if (schema_is(described, "number") == true) {
                    /* Seconds with nine digits of fraction; a negative instant keeps whole seconds. */
                    if (at.unix_seconds < 0i64 || at.nanoseconds == 0u32) {
                        std.string::string whole = f"{at.unix_seconds}";
                        return json_number(whole.as_str());
                    }
                    u32 fraction = at.nanoseconds + 1000000000u32;
                    std.string::string digits = f"{fraction}";
                    std.string::string seconds = f"{at.unix_seconds}.";
                    const u8[] digit_bytes = digits.as_bytes();
                    seconds.append(core::validate_utf8(digit_bytes[1usize..len(digit_bytes)]));
                    return json_number(seconds.as_str());
                }
                std.string::string written = std.time::format_rfc3339(at, 6u32);
                return std.json::from_string(written.as_bytes());
            }
            bool wants_string = schema_is(described, "string");
            if (wants_string == false && is_json_oid(oid) == true) {
                return std.json::parse(text->as_bytes());
            }
            if (is_array_oid(oid) == true && schema_is(described, "array") == true) {
                array<o<std.string::string>> elements = array_elements(text->as_bytes());
                std.json::value items = std.json::array();
                const std.json::value* item_schema = described;
                switch (std.json::find(described, "items")) {
                case variant o::some(found): item_schema = *found;
                case variant o::none: break;
                }
                for (usize index = 0usize; index < len(elements); index += 1usize) {
                    switch (elements[index]) {
                    case variant o::some(element): std.json::append(&items, text_for(non_null(item_schema), element->as_str()));
                    case variant o::none: std.json::append(&items, std.json::null());
                    }
                }
                return move items;
            }
            return text_for(described, text->as_str());
        }
    } catch (std.json::error rejected) {
        (move rejected) as void;
    } catch (std.time::time_error rejected) {
        rejected as void;
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_value, "a value cannot be written as JSON");
}

@generic<T: json_decode>
protected std.json::value schema_for() throws pg_error, std.alloc::alloc_error {
    try {
        return std.json::schema::<T>();
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::unsupported, "the type has no JSON Schema");
}

/* The JSON object of a row for the schema of a struct: each field takes the column of its JSON
   name; NULL is null; a missing column leaves an optional field out. */
protected std.json::value row_document(const rows* table, usize at, const std.json::value* described)
    throws pg_error, std.alloc::alloc_error {
    throw (at >= len(table->items)) failure(error_code::column_range, "the result has no such row");
    try {
        std.json::value document = std.json::object();
        switch (std.json::find(described, "properties")) {
        case variant o::some(properties):
            for (usize index = 0usize; index < std.json::len(*properties); index += 1usize) {
                str key = std.json::key_at(*properties, index);
                switch (std.json::get(*properties, index)) {
                case variant o::some(property):
                    const u8[] wanted = key;
                    usize found = len(table->columns);
                    for (usize column = 0usize; column < len(table->columns); column += 1usize) {
                        if (found == len(table->columns) && std.bytes::equal(table->columns[column].name.as_bytes(), wanted) == true) {
                            found = column;
                        }
                    }
                    if (found == len(table->columns)) {
                        if (required_property(described, key) == true && accepts_null(*property) == false) {
                            pg_error missing = failure(error_code::missing_column, "the result has no column for a field");
                            missing.detail.append(key);
                            throw move missing;
                        }
                    } else {
                        const value* cell = &table->items[at].values[found];
                        bool null_cell = false;
                        switch (*cell) {
                        case variant value::null_value: null_cell = true;
                        default: break;
                        }
                        if (null_cell == true && accepts_null(*property) == false && required_property(described, key) == true) {
                            pg_error refused = failure(error_code::null_value, "a column of a field that is not o<T> is NULL");
                            refused.detail.append(key);
                            throw move refused;
                        }
                        if (null_cell == false || accepts_null(*property) == true) {
                            std.json::insert(&document, wanted, cell_json(cell, table->columns[found].type_oid, *property));
                        }
                    }
                case variant o::none: key as void;
                }
            }
        case variant o::none: break;
        }
        return move document;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_value, "a row cannot be written as JSON");
}

@generic<T: json_decode>
protected T read_row(std.json::value document) throws pg_error, std.alloc::alloc_error {
    try {
        std.string::string text = std.json::stringify(&document);
        T result = std.json::unmarshal(text.as_bytes());
        return move result;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_value, "a row does not read as the struct");
}

/* R-SLIB-PG-0015: the row at `at` as a struct whose fields take the columns of their JSON names. */
@generic<T: json_decode>
T rows::decode(const rows* this, usize at) throws pg_error, std.alloc::alloc_error {
    std.json::value described = schema_for::<T>();
    std.json::value document = row_document(this, at, &described);
    return read_row::<T>(move document);
}

/* R-SLIB-PG-0015: every row as such a struct, in order. */
@generic<T: json_decode>
array<T> rows::decode_all(const rows* this) throws pg_error, std.alloc::alloc_error {
    std.json::value described = schema_for::<T>();
    array<T> read = [];
    for (usize at = 0usize; at < len(this->items); at += 1usize) {
        std.json::value document = row_document(this, at, &described);
        T item = read_row::<T>(move document);
        try {
            read.push(move item);
        } catch (std.array::push_error<T> rejected) {
            switch (move rejected) {
            case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
            }
        }
    }
    drop described;
    return move read;
}

@generic<T: json_encode>
protected std.json::value encode_schema() throws pg_error, std.alloc::alloc_error {
    try {
        return std.json::schema::<T>();
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::unsupported, "the type has no JSON Schema");
}

/* Appends a quoted identifier, its quotes doubled. */
protected void put_identifier(std.string::string* out, str name) throws pg_error, std.alloc::alloc_error {
    bytes quoted_name = {};
    std.bytes::append_u8(&quoted_name, 34u8);
    const u8[] raw_name = name;
    for (usize index = 0usize; index < len(raw_name); index += 1usize) {
        if (raw_name[index] == 34u8) { std.bytes::append_u8(&quoted_name, 34u8); }
        std.bytes::append_u8(&quoted_name, raw_name[index]);
    }
    std.bytes::append_u8(&quoted_name, 34u8);
    std.string::string shown = text_of_bytes(quoted_name.as_slice());
    out->append(shown.as_str());
}

/* R-SLIB-PG-0015: INSERT INTO table ("f1", ...) VALUES ($1, ...) for the fields of T in order. */
@generic<T: json_encode>
std.string::string insert_statement(str table) throws pg_error, std.alloc::alloc_error {
    std.json::value described = encode_schema::<T>();
    std.string::string names = std.string::create();
    std.string::string slots = std.string::create();
    switch (std.json::find(&described, "properties")) {
    case variant o::some(properties):
        for (usize index = 0usize; index < std.json::len(*properties); index += 1usize) {
            if (index > 0usize) {
                names.append(", ");
                slots.append(", ");
            }
            put_identifier(&names, std.json::key_at(*properties, index));
            usize number = index + 1usize;
            std.string::string slot = f"${number}";
            slots.append(slot.as_str());
        }
    case variant o::none: break;
    }
    return f"INSERT INTO {table} ({names}) VALUES ({slots})";
}

/* R-SLIB-PG-0015: UPDATE table SET "f" = $i, ... WHERE "key" = $k for the fields of T, their
   parameters numbered as by parameters_of; a key that names no field is missing_column. */
@generic<T: json_encode>
std.string::string update_statement(str table, str key) throws pg_error, std.alloc::alloc_error {
    std.json::value described = encode_schema::<T>();
    std.string::string sets = std.string::create();
    usize key_number = 0usize;
    switch (std.json::find(&described, "properties")) {
    case variant o::some(properties):
        for (usize index = 0usize; index < std.json::len(*properties); index += 1usize) {
            str name = std.json::key_at(*properties, index);
            usize number = index + 1usize;
            if (same(name, key) == true) {
                key_number = number;
            } else {
                const u8[] written = sets.as_bytes();
                if (len(written) > 0usize) { sets.append(", "); }
                put_identifier(&sets, name);
                std.string::string slot = f" = ${number}";
                sets.append(slot.as_str());
            }
        }
    case variant o::none: break;
    }
    if (key_number == 0usize) {
        pg_error missing = failure(error_code::missing_column, "the key names no field");
        missing.detail.append(key);
        throw move missing;
    }
    std.string::string where = std.string::create();
    put_identifier(&where, key);
    return f"UPDATE {table} SET {sets} WHERE {where} = ${key_number}";
}

/* The parameter of one element of a JSON array. */
protected value element_parameter(const std.json::value* element) throws pg_error, std.alloc::alloc_error {
    std.json::value_kind kind = std.json::kind(element);
    if (kind == std.json::value_kind::boolean) { return value::boolean(std.json::boolean(element)); }
    if (kind == std.json::value_kind::string || kind == std.json::value_kind::number) {
        return value::of_text(std.json::text(element));
    }
    if (kind == std.json::value_kind::null) { return value::null_value; }
    try {
        return value::text(std.json::stringify(element));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_value, "an element cannot be a parameter");
}

/* The parameter of a JSON value for the schema of its field. */
protected value parameter_of(const std.json::value* item, const std.json::value* property) throws pg_error, std.alloc::alloc_error {
    std.json::value_kind kind = std.json::kind(item);
    if (kind == std.json::value_kind::null) { return value::null_value; }
    if (kind == std.json::value_kind::boolean) { return value::boolean(std.json::boolean(item)); }
    if (kind == std.json::value_kind::string || kind == std.json::value_kind::number) {
        return value::of_text(std.json::text(item));
    }
    if (kind == std.json::value_kind::array && schema_is(non_null(property), "array") == true) {
        array<value> elements = [];
        for (usize index = 0usize; index < std.json::len(item); index += 1usize) {
            switch (std.json::get(item, index)) {
            case variant o::some(element): push_value(&elements, element_parameter(*element));
            case variant o::none: break;
            }
        }
        return value::of_array(&elements);
    }
    try {
        return value::text(std.json::stringify(item));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_value, "a field cannot be a parameter");
}

/* R-SLIB-PG-0015: the parameters of the fields of a struct in order: strings and numbers as
   their text, booleans, null as NULL, arrays as array literals and other values as JSON text. */
@generic<T: json_encode>
array<value> parameters_of(const T* item) throws pg_error, std.alloc::alloc_error {
    std.json::value described = encode_schema::<T>();
    array<value> parameters = [];
    try {
        std.string::string text = std.json::marshal(item);
        std.json::value document = std.json::parse(text.as_bytes());
        throw (std.json::kind(&document) != std.json::value_kind::object)
            failure(error_code::invalid_value, "the struct is not written as an object");
        switch (std.json::find(&described, "properties")) {
        case variant o::some(properties):
            for (usize index = 0usize; index < std.json::len(*properties); index += 1usize) {
                str key = std.json::key_at(*properties, index);
                const u8[] wanted = key;
                switch (std.json::get(*properties, index)) {
                case variant o::some(property):
                    value converted = value::null_value;
                    switch (std.json::find(&document, wanted)) {
                    case variant o::some(found): converted = parameter_of(*found, *property);
                    case variant o::none: break;
                    }
                    push_value(&parameters, move converted);
                case variant o::none: break;
                }
            }
        case variant o::none: break;
        }
    } catch (std.json::error rejected) {
        (move rejected) as void;
        throw failure(error_code::invalid_value, "the struct cannot be written as JSON");
    }
    return move parameters;
}

/* ---- Migrations (R-SLIB-PG-0016) ---- */


/* R-SLIB-PG-0016: one version of the schema of a database and the statements that make it. */
struct migration {
    u64 version;
    std.string::string name;
    std.string::string sql;
};

protected std.string::string checksum_of(const std.string::string* sql) throws std.alloc::alloc_error {
    std.hash::sha256_digest digest = std.hash::sha256(sql->as_bytes());
    return std.encoding::encode_hex(&digest.bytes);
}

/* The lock key of migrations: the bytes of "r-migr" read as a number. */
protected const i64 migration_lock = 125806466509938i64;

protected pg_error mismatch(str message, u64 version) throws std.alloc::alloc_error {
    pg_error refused = failure(error_code::migration_mismatch, message);
    std.string::string shown = f"version {version}";
    refused.detail.append(shown.as_str());
    return move refused;
}

/* The pending migrations after checking the steps against the applied versions. */
protected array<usize> pending_of(const array<migration>* steps, const rows* applied) throws pg_error, std.alloc::alloc_error {
    for (usize index = 1usize; index < len(*steps); index += 1usize) {
        throw ((*steps)[index].version <= (*steps)[index - 1usize].version)
            mismatch("the versions of the migrations do not ascend", (*steps)[index].version);
    }
    u64 highest = 0u64;
    for (usize index = 0usize; index < len(applied->items); index += 1usize) {
        u64 version = applied->items[index].integer(0usize) as u64;
        usize found = len(*steps);
        for (usize at = 0usize; at < len(*steps); at += 1usize) {
            if ((*steps)[at].version == version) { found = at; }
        }
        throw (found == len(*steps)) mismatch("the database has a migration that the list does not", version);
        str recorded = applied->items[index].text(1usize);
        std.string::string expected = checksum_of(&(*steps)[found].sql);
        throw (same(expected.as_str(), recorded) == false) mismatch("an applied migration has changed", version);
        if (version > highest) { highest = version; }
    }
    array<usize> pending = [];
    for (usize at = 0usize; at < len(*steps); at += 1usize) {
        u64 version = (*steps)[at].version;
        bool done = false;
        for (usize index = 0usize; index < len(applied->items); index += 1usize) {
            if (applied->items[index].integer(0usize) as u64 == version) { done = true; }
        }
        if (done == false) {
            throw (version < highest) mismatch("a migration is older than one already applied", version);
            try {
                pending.push(at);
            } catch (std.array::push_error<usize> rejected) {
                switch (move rejected) {
                case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
                }
            }
        }
    }
    return move pending;
}

protected async u64 run_migrate(arc shared_state core, array<migration> steps) throws pg_error, std.error::fault {
    i64 key = migration_lock;
    std.string::string lock = f"SELECT pg_advisory_lock({key})";
    std.string::string unlock = f"SELECT pg_advisory_unlock({key})";
    rows locked = await run_query(std.arc::clone(&core), move lock, [], std.string::create(), true);
    drop locked;
    u64 applied_count = 0u64;
    try {
        await run_script(std.arc::clone(&core), std.string::from_str(
            "CREATE TABLE IF NOT EXISTS r_schema_migrations (version bigint PRIMARY KEY, name text NOT NULL, "
            "checksum text NOT NULL, applied_at timestamptz NOT NULL DEFAULT now())"));
        rows applied = await run_query(std.arc::clone(&core),
                                       std.string::from_str("SELECT version, checksum FROM r_schema_migrations ORDER BY version"),
                                       [], std.string::create(), true);
        array<usize> pending = pending_of(&steps, &applied);
        for (usize index = 0usize; index < len(pending); index += 1usize) {
            usize position = pending[index];
            u64 version_number = steps[position].version;
            std.string::string sql = std.string::from_str(steps[position].sql.as_str());
            std.string::string name = std.string::from_str(steps[position].name.as_str());
            std.string::string checksum = checksum_of(&steps[position].sql);
            std.string::string version_text = f"{version_number}";
            array<value> recorded = [];
            push_value(&recorded, value::text(move version_text));
            push_value(&recorded, value::text(move name));
            push_value(&recorded, value::text(move checksum));
            await run_script(std.arc::clone(&core), std.string::from_str("BEGIN"));
            try {
                await run_script(std.arc::clone(&core), move sql);
                rows inserted = await run_query(std.arc::clone(&core),
                    std.string::from_str("INSERT INTO r_schema_migrations (version, name, checksum) VALUES ($1, $2, $3)"),
                    move recorded, std.string::create(), true);
                drop inserted;
                await run_script(std.arc::clone(&core), std.string::from_str("COMMIT"));
                applied_count += 1u64;
            } catch (pg_error refused) {
                await run_script(std.arc::clone(&core), std.string::from_str("ROLLBACK"));
                throw move refused;
            }
        }
    } catch (pg_error refused) {
        rows released = await run_query(std.arc::clone(&core), move unlock, [], std.string::create(), true);
        drop released;
        throw move refused;
    }
    rows released = await run_query(std.arc::clone(&core), move unlock, [], std.string::create(), true);
    drop released;
    return applied_count;
}

/* R-SLIB-PG-0016: applies the migrations of a list that the database does not have, each in a
   transaction of its own, under an advisory lock, and returns how many it applied. */
task<u64 throws pg_error, std.error::fault> migrate(const connection* conn, array<migration> steps)
    throws std.async::start_error {
    return run_migrate(std.arc::clone(&conn->core), move steps);
}
