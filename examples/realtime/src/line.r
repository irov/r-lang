module example.realtime.line;
import std.http;
import std.text;
import std.tls;
import example.realtime.room;

/* A protocol of its own behind an HTTP upgrade: the client sends one line, the server answers it
   in upper case and ends the connection. */

protected void put(std.http::headers* fields, str name, str value) throws std.alloc::alloc_error {
    try {
        fields->add(name, value);
    } catch (std.http::http_error rejected) {
        // The names and values below are valid fields.
        rejected as void;
    }
}

/* Whether the bytes hold a line feed. */
protected bool has_line_end(const u8[] data) {
    for (usize index = 0usize; index < len(data); index += 1usize) {
        if (data[index] == 10u8) { return true; }
    }
    return false;
}

/* The bytes as text; bytes that are not UTF-8 read as "?". */
protected std.string::string text_of(const u8[] data) throws std.alloc::alloc_error {
    try {
        return std.string::from_utf8(data);
    } catch (std.string::string_error rejected) {
        rejected as void;
    }
    return std.string::from_str("?");
}

/* Reads the line of the client, the bytes read with the request first, and answers it. */
@scoped
protected async void shout(const std.http::upgraded* switched) throws std.error::fault {
    bytes received = {};
    std.bytes::append(&received, switched->buffered.as_slice());
    u8[64] chunk = {};
    bytes answer = {};
    task_scope(1) io {
        while (has_line_end(received.as_slice()) == false) {
            usize count = await switched->transport.read_into(&chunk);
            if (count == 0usize) { break; }
            std.bytes::append(&received, chunk[0usize..count]);
        }
        for (usize index = 0usize; index < len(received); index += 1usize) {
            u32 value = received[index] as u32;
            if (value >= 97u32 && value <= 122u32) { value -= 32u32; }
            std.bytes::append_u8(&answer, value as u8);
        }
        await switched->transport.write_all_from(answer.as_slice());
        await switched->transport.shutdown();
    }
}

/* The upgrade route /line; any request without "Upgrade: line" is refused with 400. */
async void line(arc example.realtime.room::Room state, std.http::request incoming,
                std.http::upgrade connection)
    throws std.error::fault {
    drop state;
    bool wanted = incoming.headers.has_token("Upgrade", "line");
    drop incoming;
    if (wanted == false) {
        await (move connection).refuse(std.http::response::text(400u16, "line only"));
        return;
    }
    std.http::headers fields = std.http::headers::create();
    put(&fields, "Upgrade", "line");
    put(&fields, "Connection", "Upgrade");
    std.http::upgraded switched = await (move connection).accept(move fields);
    task_scope(1) io { await shout(&switched); }
}

/* Writes the text on a switched connection, ends its write direction and returns the answer. */
@scoped
protected async std.string::string exchange(const std.http::upgraded* switched, str text)
    throws std.error::fault {
    bytes received = {};
    std.bytes::append(&received, switched->buffered.as_slice());
    u8[64] chunk = {};
    task_scope(1) io {
        await switched->transport.write_all_from(text);
        await switched->transport.shutdown();
        while (true) {
            usize count = await switched->transport.read_into(&chunk);
            if (count == 0usize) { break; }
            std.bytes::append(&received, chunk[0usize..count]);
        }
    }
    return text_of(received.as_slice());
}

/* Asks the server at the URL to switch to the line protocol and sends it the text; returns the
   status of the answer and, after 101, the line that came back. */
@scoped
async std.string::string ask(std.http::client* web, str address, str text)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    std.http::request asking = std.http::request::create(std.http::method::get, "/");
    put(&asking.headers, "Upgrade", "line");
    put(&asking.headers, "Connection", "Upgrade");
    o<std.http::upgraded> taken = o::none;
    std.string::string report = std.string::create();
    task_scope(1) switching {
        std.http::handshake answered = await web->upgrade(move asking, address);
        u16 status = answered.answer.status;
        std.string::string shown = f"{status}";
        std.string::append_str(&report, shown.as_str());
        o<std.http::upgraded> received = core::replace(&answered.connection, o::none);
        o<std.http::upgraded> old = core::replace(&taken, move received);
        drop old;
    }
    switch (move taken) {
    case variant o::some(move switched):
        task_scope(1) io {
            std.string::string back = await exchange(&switched, text);
            std.string::append_str(&report, " ");
            std.string::append_str(&report, std.text::trim(back.as_str()));
        }
    case variant o::none: break;
    }
    return move report;
}
