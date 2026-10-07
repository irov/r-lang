module example.http.catalog;
import std.http;

/* An item of the catalog as its JSON writes it. */
struct Item { u32 id; std.string::string name; u32 price; };

/* The body of POST /items. */
struct NewItem { std.string::string name; u32 price; };

/* The state that every handler shares: the items and the number of items created so far. */
struct Catalog { array<Item> items; atomic u32 created; };

protected void append_item(array<Item>* items, u32 id, str name, u32 price)
    throws std.alloc::alloc_error {
    try {
        items->push(Item {.id = id, .name = std.string::from_str(name), .price = price});
    } catch (std.array::push_error<Item> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

Catalog create() throws std.alloc::alloc_error {
    array<Item> items = std.array::create::<Item>();
    append_item(&items, 1u32, "chair", 45u32);
    append_item(&items, 2u32, "lamp", 30u32);
    append_item(&items, 3u32, "shelf", 80u32);
    return Catalog {.items = move items, .created = 0u32};
}

protected std.http::response error_json(u16 status, str message) throws std.alloc::alloc_error {
    std.string::string body = std.string::from_str("{\"error\":\"");
    std.string::append_str(&body, message);
    std.string::append_str(&body, "\"}");
    return std.http::response::json(status, body);
}

async std.http::response health(arc Catalog state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    drop incoming;
    return std.http::response::json(200u16, "{\"status\":\"ok\"}");
}

async std.http::response list_items(arc Catalog state, std.http::request incoming)
    throws std.error::fault {
    drop incoming;
    try {
        std.string::string body = std.json::marshal(&state->items);
        return std.http::response::json(200u16, body);
    } catch (std.json::error rejected) {
        drop rejected;
    }
    return error_json(500u16, "encoding failed");
}

async std.http::response item(arc Catalog state, std.http::request incoming)
    throws std.error::fault {
    u32 wanted = 0u32;
    switch (incoming.param("id")) {
    case variant o::some(text):
        try {
            wanted = std.convert::parse_u32(*text, 10u32);
        } catch (std.convert::parse_error rejected) {
            rejected as void;
            return error_json(400u16, "the id is not a number");
        }
    case variant o::none: break;
    }
    for (usize index = 0usize; index < len(state->items); index += 1usize) {
        if (state->items[index].id == wanted) {
            try {
                std.string::string body = std.json::marshal(&state->items[index]);
                return std.http::response::json(200u16, body);
            } catch (std.json::error rejected) {
                drop rejected;
                return error_json(500u16, "encoding failed");
            }
        }
    }
    std.string::string message = f"no item {wanted}";
    return error_json(404u16, message);
}

async std.http::response add(arc Catalog state, std.http::request incoming)
    throws std.error::fault {
    try {
        std.string::string text = std.string::from_utf8(incoming.body.as_slice());
        NewItem given = std.json::unmarshal(text);
        const Catalog* shared = &*state;
        u32 created = core::atomic_fetch_add(&shared->created, 1u32, core::memory_order::relaxed);
        std.string::string name = core::replace(&given.name, std.string::create());
        Item made = {.id = (len(shared->items) as u32) + created + 1u32, .name = move name,
                     .price = given.price};
        std.string::string body = std.json::marshal(&made);
        std.http::response result = std.http::response::json(201u16, body);
        return move result;
    } catch (std.json::error rejected) {
        drop rejected;
    } catch (std.string::string_error rejected) {
        rejected as void;
    }
    return error_json(400u16, "invalid item");
}

async std.http::response old_items(arc Catalog state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    drop incoming;
    std.http::response result = std.http::response::create(301u16);
    try {
        result.headers.add("Location", "/items");
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    return move result;
}

/* Streams the price of every item as a server-sent event. */
async void prices(arc Catalog state, std.http::request incoming, std.http::body_writer writer)
    throws std.error::fault {
    drop incoming;
    std.http::response head = std.http::response::create(200u16);
    try {
        head.headers.add("Content-Type", "text/event-stream");
        head.headers.add("Cache-Control", "no-cache");
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    task_scope(1) begin {
        bool open = await writer.start(move head);
        if (open == false) { return; }
    }
    for (usize index = 0usize; index < len(state->items); index += 1usize) {
        u32 id = state->items[index].id;
        u32 price = state->items[index].price;
        std.string::string name = std.string::from_str(state->items[index].name);
        std.string::string event_id = f"{id}";
        std.string::string data = f"{name}={price}";
        std.string::string text = std.http::sse_event("price", event_id, data);
        task_scope(1) io {
            bool sent = await writer.send_text(text);
            if (sent == false) { return; }
        }
    }
}

/* Writing requests need the API key. */
o<std.http::response> require_key(const Catalog* state, const std.http::request* incoming)
    throws std.alloc::alloc_error {
    state as void;
    if (incoming->method != std.http::method::post) { return o::none; }
    switch (incoming->headers.get("X-Api-Key")) {
    case variant o::some(key):
        if (std.bytes::equal(*key, "secret") == true) { return o::none; }
    case variant o::none: break;
    }
    return o::some(error_json(401u16, "missing API key"));
}

/* Every response names the service. */
void name_service(std.http::method sent, str target, std.http::response* result)
    throws std.alloc::alloc_error {
    sent as void;
    target as void;
    try {
        result->headers.add("X-Service", "catalog");
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
}

std.http::router<Catalog> routes() throws std.http::http_error, std.alloc::alloc_error {
    std.http::router<Catalog> result = std.http::router<Catalog>::create();
    result.add(std.http::method::get, "/health", health);
    result.add(std.http::method::get, "/items", list_items);
    result.add(std.http::method::get, "/items/{id}", item);
    result.add(std.http::method::post, "/items", add);
    result.add(std.http::method::get, "/old-items", old_items);
    result.add_stream(std.http::method::get, "/prices", prices);
    result.before(require_key);
    result.after(name_service);
    return move result;
}
