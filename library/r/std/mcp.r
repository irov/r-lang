module std.mcp;
import std.stream;
import std.bufio;
import std.text;
import std.encoding;
import std.url;
import std.cmp;
import std.jsonrpc;
import std.http;
import std.tls;
import std.service;
import std.time;
import std.uuid;

/* R-SLIB-MCP-0001: the protocol revision that this module speaks, and no other. */
const constexpr str protocol_version = "2026-07-28";

/* R-SLIB-MCP-0001: the error codes that MCP adds to those of JSON-RPC. */
const i64 header_mismatch = -32020i64;
const i64 missing_required_client_capability = -32021i64;
const i64 unsupported_protocol_version = -32022i64;

/* The reserved keys of _meta. */
protected const constexpr str key_version = "io.modelcontextprotocol/protocolVersion";
protected const constexpr str key_capabilities = "io.modelcontextprotocol/clientCapabilities";
protected const constexpr str key_client = "io.modelcontextprotocol/clientInfo";
protected const constexpr str key_level = "io.modelcontextprotocol/logLevel";
protected const constexpr str key_server = "io.modelcontextprotocol/serverInfo";
protected const constexpr str key_subscription = "io.modelcontextprotocol/subscriptionId";

/* R-SLIB-MCP-0001: a failure of this module: a JSON-RPC error that a peer sent or that a message
   breaks, with its code and message. */
error mcp_error { i64 code; std.string::string message; };

protected mcp_error failure(i64 code, str message) throws std.alloc::alloc_error {
    return mcp_error {.code = code, .message = std.string::from_str(message)};
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

protected void add_text(array<std.string::string>* target, str text) throws std.alloc::alloc_error {
    append(target, std.string::from_str(text));
}

/* The value under name in parallel arrays of names and values. */
protected o<str> lookup(const array<std.string::string>* names, const array<std.string::string>* values,
                        str name) {
    for (usize index = 0usize; index < len(*names); index += 1usize) {
        if (std.bytes::equal((*names)[index], name) == true) {
            return o::some((*values)[index]);
        }
    }
    return o::none;
}

protected bool present(o<str> value) {
    switch (value) {
    case variant o::some(text):
        text as void;
        return true;
    case variant o::none: return false;
    }
}

/* ---- JSON ---- */

protected std.json::value json_number_text(str text) throws std.json::error, std.alloc::alloc_error {
    std.json::number number = std.json::parse_number(text);
    return std.json::from_number(&number);
}

protected std.json::value json_unsigned(u64 value) throws std.json::error, std.alloc::alloc_error {
    std.string::string text = f"{value}";
    return json_number_text(text);
}

protected std.json::value json_float(f64 value) throws std.json::error, std.alloc::alloc_error {
    std.string::string text = f"{value}";
    return json_number_text(text);
}

protected std.json::value copy_value(const std.json::value* value) throws std.json::error, std.alloc::alloc_error {
    std.string::string text = std.json::stringify(value);
    return std.json::parse(text);
}

protected std.json::value object_or_empty(o<std.json::value> taken) throws std.json::error, std.alloc::alloc_error {
    switch (move taken) {
    case variant o::some(move value): return move value;
    case variant o::none: return std.json::object();
    }
}

protected std.json::value empty_object() throws std.alloc::alloc_error {
    try {
        return std.json::object();
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.json::null();
}

protected std.json::value value_or_null(o<std.json::value> taken) {
    switch (move taken) {
    case variant o::some(move value): return move value;
    case variant o::none: return std.json::null();
    }
}

/* The JSON value of what std.json::marshal writes for a value. */
@generic<T: json_encode>
protected std.json::value to_value(const T* source) throws std.json::error, std.alloc::alloc_error {
    std.string::string text = std.json::marshal(source);
    return std.json::parse(text);
}

protected void put(std.json::value* object, str key, std.json::value item)
    throws std.json::error, std.alloc::alloc_error {
    std.json::insert(object, key, move item);
}

protected void put_text(std.json::value* object, str key, str text)
    throws std.json::error, std.alloc::alloc_error {
    std.json::insert(object, key, std.json::from_string(text));
}

protected void put_flag(std.json::value* object, str key) throws std.json::error, std.alloc::alloc_error {
    std.json::insert(object, key, std.json::from_bool(true));
}

/* The member of an object, none when it is absent or the value is no object. */
protected o<const std.json::value*> member(const std.json::value* object, str name) {
    if (std.json::kind(object) != std.json::value_kind::object) { return o::none; }
    return std.json::find(object, name);
}

/* The text of a string member, none when it is absent or of another kind. */
protected o<str> member_text(const std.json::value* object, str name) {
    switch (member(object, name)) {
    case variant o::some(item):
        if (std.json::kind(*item) == std.json::value_kind::string) { return o::some(std.json::text(*item)); }
    case variant o::none: break;
    }
    return o::none;
}

protected bool has_member(const std.json::value* object, str name) {
    switch (member(object, name)) {
    case variant o::some(item):
        item as void;
        return true;
    case variant o::none: return false;
    }
}

protected bool member_is(const std.json::value* object, str name, std.json::value_kind wanted) {
    switch (member(object, name)) {
    case variant o::some(item): return std.json::kind(*item) == wanted;
    case variant o::none: return false;
    }
}

protected bool member_true(const std.json::value* object, str name) {
    switch (member(object, name)) {
    case variant o::some(item):
        return std.json::kind(*item) == std.json::value_kind::boolean && std.json::boolean(*item) == true;
    case variant o::none: return false;
    }
}

protected std.string::string owned(o<str> text) throws std.alloc::alloc_error {
    switch (text) {
    case variant o::some(value): return std.string::from_str(*value);
    case variant o::none: return std.string::create();
    }
}

protected std.json::error shape_error() {
    return std.json::error {.code = std.json::error_code::type, .offset = 0usize,
                            .pointer = std.string::create()};
}

/* ---- Protocol types ---- */

/* R-SLIB-MCP-0002: the name and version of a server or client. */
struct implementation {
    std.string::string name;
    @json(optional, omitempty) std.string::string title;
    std.string::string version;
    @json(optional, omitempty) std.string::string description;
};

implementation implementation::create(str name, str version) throws std.alloc::alloc_error {
    return implementation {.name = std.string::from_str(name), .title = std.string::create(),
                           .version = std.string::from_str(version),
                           .description = std.string::create()};
}

/* R-SLIB-MCP-0003: the hints of a tool; none leaves a hint at its default. */
struct tool_annotations {
    @json(optional, omitempty) std.string::string title;
    @json(name = "readOnlyHint", optional, omitnone) o<bool> read_only;
    @json(name = "destructiveHint", optional, omitnone) o<bool> destructive;
    @json(name = "idempotentHint", optional, omitnone) o<bool> idempotent;
    @json(name = "openWorldHint", optional, omitnone) o<bool> open_world;
};

/* R-SLIB-MCP-0003: a tool that a server offers: its name, description, the JSON Schema of its
   arguments, whose root is an object, and optionally the schema of its structured result. */
struct tool {
    std.string::string name;
    @json(optional, omitempty) std.string::string title;
    @json(optional, omitempty) std.string::string description;
    @json(name = "inputSchema") std.json::value input_schema;
    @json(name = "outputSchema", optional, omitnone) o<std.json::value> output_schema;
    @json(optional, omitnone) o<tool_annotations> annotations;
};

/* R-SLIB-MCP-0003: the input schema of a tool without arguments:
   {"type":"object","additionalProperties":false}. */
std.json::value no_arguments() throws std.json::error, std.alloc::alloc_error {
    std.json::value schema = std.json::object();
    put_text(&schema, "type", "object");
    std.json::insert(&schema, "additionalProperties", std.json::from_bool(false));
    return move schema;
}

/* R-SLIB-MCP-0003: a tool with a name, a description and the schema of its arguments. */
tool tool::create(str name, str description, std.json::value input_schema) throws std.alloc::alloc_error {
    return tool {.name = std.string::from_str(name), .title = std.string::create(),
                 .description = std.string::from_str(description), .input_schema = move input_schema,
                 .output_schema = o::none, .annotations = o::none};
}

/* R-SLIB-MCP-0004: the contents of a resource at a URI: text, or binary data that travels as
   base64. */
struct contents {
    std.string::string uri;
    std.string::string mime_type;
    std.string::string text;
    bytes blob;
    bool binary;
};

contents contents::of_text(str uri, str mime_type, str text) throws std.alloc::alloc_error {
    return contents {.uri = std.string::from_str(uri), .mime_type = std.string::from_str(mime_type),
                     .text = std.string::from_str(text), .blob = {}, .binary = false};
}

contents contents::of_blob(str uri, str mime_type, bytes data) throws std.alloc::alloc_error {
    return contents {.uri = std.string::from_str(uri), .mime_type = std.string::from_str(mime_type),
                     .text = std.string::create(), .blob = move data, .binary = true};
}

/* R-SLIB-MCP-0005: a resource that a server offers. */
struct resource {
    std.string::string uri;
    std.string::string name;
    @json(optional, omitempty) std.string::string title;
    @json(optional, omitempty) std.string::string description;
    @json(name = "mimeType", optional, omitempty) std.string::string mime_type;
    @json(optional, omitnone) o<u64> size;
};

resource resource::create(str uri, str name, str mime_type) throws std.alloc::alloc_error {
    return resource {.uri = std.string::from_str(uri), .name = std.string::from_str(name),
                     .title = std.string::create(), .description = std.string::create(),
                     .mime_type = std.string::from_str(mime_type), .size = o::none};
}

/* R-SLIB-MCP-0005: a family of resources named by a URI template of RFC 6570 with simple
   variables: literal text and {name}, which matches a nonempty part without '/', or {+name},
   which matches a nonempty rest that may contain '/'. */
struct resource_template {
    @json(name = "uriTemplate") std.string::string uri_template;
    std.string::string name;
    @json(optional, omitempty) std.string::string title;
    @json(optional, omitempty) std.string::string description;
    @json(name = "mimeType", optional, omitempty) std.string::string mime_type;
};

resource_template resource_template::create(str uri_template, str name, str mime_type)
    throws std.alloc::alloc_error {
    return resource_template {.uri_template = std.string::from_str(uri_template),
                              .name = std.string::from_str(name), .title = std.string::create(),
                              .description = std.string::create(),
                              .mime_type = std.string::from_str(mime_type)};
}

/* R-SLIB-MCP-0004: the data of an image or audio block: its bytes, base64 on the wire, and its
   media type. */
struct media { bytes data; std.string::string mime_type; };

/* R-SLIB-MCP-0004: one block of content. */
enum content {
    text(std.string::string),
    image(media),
    audio(media),
    link(resource),
    embedded(contents),
};

/* The JSON of the contents of a resource: {"uri", "mimeType"?, "text" | "blob"}. */
protected std.json::value contents_value(const contents* item) throws std.json::error, std.alloc::alloc_error {
    std.json::value object = std.json::object();
    put_text(&object, "uri", item->uri);
    if (std.string::len(&item->mime_type) != 0usize) { put_text(&object, "mimeType", item->mime_type); }
    if (item->binary == true) {
        std.string::string encoded = std.encoding::encode_base64(item->blob.as_slice());
        put_text(&object, "blob", encoded);
    } else {
        put_text(&object, "text", item->text);
    }
    return move object;
}

protected std.json::value media_value(str kind, const media* item) throws std.json::error, std.alloc::alloc_error {
    std.json::value object = std.json::object();
    put_text(&object, "type", kind);
    std.string::string encoded = std.encoding::encode_base64(item->data.as_slice());
    put_text(&object, "data", encoded);
    put_text(&object, "mimeType", item->mime_type);
    return move object;
}

/* R-SLIB-MCP-0004: a content block is written with its "type" member. */
std.json::value content::json_marshal(const content* value) throws std.json::error, std.alloc::alloc_error {
    switch (*value) {
    case variant content::text(text):
        std.json::value object = std.json::object();
        put_text(&object, "type", "text");
        put_text(&object, "text", *text);
        return move object;
    case variant content::image(item): return media_value("image", item);
    case variant content::audio(item): return media_value("audio", item);
    case variant content::link(item):
        std.json::value object = std.json::object();
        put_text(&object, "type", "resource_link");
        std.json::value fields = to_value(item);
        for (usize index = 0usize; index < std.json::len(&fields); index += 1usize) {
            str key = std.json::key_at(&fields, index);
            switch (std.json::find(&fields, key)) {
            case variant o::some(field): put(&object, key, copy_value(*field));
            case variant o::none: break;
            }
        }
        return move object;
    case variant content::embedded(item):
        std.json::value object = std.json::object();
        put_text(&object, "type", "resource");
        put(&object, "resource", contents_value(item));
        return move object;
    }
}

protected bytes decoded_base64(o<str> text) throws std.json::error, std.alloc::alloc_error {
    switch (text) {
    case variant o::some(value):
        try {
            return std.encoding::decode_base64(*value);
        } catch (std.convert::parse_error rejected) {
            rejected as void;
        }
    case variant o::none: break;
    }
    throw shape_error();
}

/* The contents of a resource read from its JSON. */
protected contents contents_of(const std.json::value* object) throws std.json::error, std.alloc::alloc_error {
    throw (member_is(object, "uri", std.json::value_kind::string) == false) shape_error();
    bool text = has_member(object, "text");
    bool blob = has_member(object, "blob");
    throw (text == blob) shape_error();
    if (text == true) {
        throw (member_is(object, "text", std.json::value_kind::string) == false) shape_error();
        return contents {.uri = owned(member_text(object, "uri")), .mime_type = owned(member_text(object, "mimeType")),
                         .text = owned(member_text(object, "text")), .blob = {}, .binary = false};
    }
    return contents {.uri = owned(member_text(object, "uri")), .mime_type = owned(member_text(object, "mimeType")),
                     .text = std.string::create(), .blob = decoded_base64(member_text(object, "blob")),
                     .binary = true};
}

protected media media_of(const std.json::value* object) throws std.json::error, std.alloc::alloc_error {
    throw (member_is(object, "mimeType", std.json::value_kind::string) == false) shape_error();
    return media {.data = decoded_base64(member_text(object, "data")),
                  .mime_type = owned(member_text(object, "mimeType"))};
}

protected o<u64> unsigned_member(const std.json::value* object, str name) {
    switch (member(object, name)) {
    case variant o::some(number):
        if (std.json::kind(*number) == std.json::value_kind::number) {
            try {
                return o::some(std.convert::parse_u64(std.json::text(*number), 10u32));
            } catch (std.convert::parse_error rejected) {
                rejected as void;
            }
        }
    case variant o::none: break;
    }
    return o::none;
}

protected resource link_of(const std.json::value* value) throws std.json::error, std.alloc::alloc_error {
    throw (member_is(value, "uri", std.json::value_kind::string) == false ||
           member_is(value, "name", std.json::value_kind::string) == false) shape_error();
    return resource {.uri = owned(member_text(value, "uri")), .name = owned(member_text(value, "name")),
                     .title = owned(member_text(value, "title")),
                     .description = owned(member_text(value, "description")),
                     .mime_type = owned(member_text(value, "mimeType")), .size = unsigned_member(value, "size")};
}

/* R-SLIB-MCP-0004: a content block is read by its "type" member. */
content content::json_unmarshal(const std.json::value* value) throws std.json::error, std.alloc::alloc_error {
    str name = "";
    switch (member_text(value, "type")) {
    case variant o::some(kind): name = *kind;
    case variant o::none: throw shape_error();
    }
    if (std.bytes::equal(name, "text") == true) {
        throw (member_is(value, "text", std.json::value_kind::string) == false) shape_error();
        return content::text(owned(member_text(value, "text")));
    }
    if (std.bytes::equal(name, "image") == true) { return content::image(media_of(value)); }
    if (std.bytes::equal(name, "audio") == true) { return content::audio(media_of(value)); }
    if (std.bytes::equal(name, "resource") == true) {
        switch (member(value, "resource")) {
        case variant o::some(inner): return content::embedded(contents_of(*inner));
        case variant o::none: throw shape_error();
        }
    }
    throw (std.bytes::equal(name, "resource_link") == false) shape_error();
    return content::link(link_of(value));
}

/* R-SLIB-MCP-0004: the JSON Schema of a content block, one of its five kinds by "type". */
std.json::value content::json_schema() throws std.json::error, std.alloc::alloc_error {
    return std.json::parse(
        "{\"oneOf\":["
        "{\"type\":\"object\",\"properties\":{\"type\":{\"const\":\"text\"},\"text\":{\"type\":\"string\"}},"
        "\"required\":[\"type\",\"text\"]},"
        "{\"type\":\"object\",\"properties\":{\"type\":{\"enum\":[\"image\",\"audio\"]},"
        "\"data\":{\"type\":\"string\"},\"mimeType\":{\"type\":\"string\"}},"
        "\"required\":[\"type\",\"data\",\"mimeType\"]},"
        "{\"type\":\"object\",\"properties\":{\"type\":{\"const\":\"resource_link\"},"
        "\"uri\":{\"type\":\"string\"},\"name\":{\"type\":\"string\"}},"
        "\"required\":[\"type\",\"uri\",\"name\"]},"
        "{\"type\":\"object\",\"properties\":{\"type\":{\"const\":\"resource\"},"
        "\"resource\":{\"type\":\"object\",\"properties\":{\"uri\":{\"type\":\"string\"},"
        "\"mimeType\":{\"type\":\"string\"},\"text\":{\"type\":\"string\"},\"blob\":{\"type\":\"string\"}},"
        "\"required\":[\"uri\"]}},"
        "\"required\":[\"type\",\"resource\"]}]}");
}

/* R-SLIB-MCP-0004: a text block. */
content content::of_text(str text) throws std.alloc::alloc_error {
    return content::text(std.string::from_str(text));
}

/* R-SLIB-MCP-0006: the result of a tool call: its content, optionally a structured result that
   conforms to the output schema of the tool, and whether the tool failed. */
struct tool_result {
    array<content> content;
    @json(name = "structuredContent", optional, omitnone) o<std.json::value> structured;
    @json(name = "isError", optional, omitzero) bool is_error;
};

/* R-SLIB-MCP-0006: a result of one text block. */
tool_result tool_result::of_text(str text) throws std.alloc::alloc_error {
    array<content> blocks = std.array::create::<content>();
    append(&blocks, content::of_text(text));
    return tool_result {.content = move blocks, .structured = o::none, .is_error = false};
}

/* R-SLIB-MCP-0006: a failure of the tool for the model to read: one text block and isError. */
tool_result tool_result::of_error(str text) throws std.alloc::alloc_error {
    array<content> blocks = std.array::create::<content>();
    append(&blocks, content::of_text(text));
    return tool_result {.content = move blocks, .structured = o::none, .is_error = true};
}

/* R-SLIB-MCP-0006: a structured result: the value that marshal writes, with its JSON text as a
   text block for clients that read only content. */
@generic<T: json_encode>
tool_result tool_result::of_structured(const T* value) throws std.json::error, std.alloc::alloc_error {
    std.string::string text = std.json::marshal(value);
    std.json::value structured = std.json::parse(text);
    array<content> blocks = std.array::create::<content>();
    append(&blocks, content::text(move text));
    return tool_result {.content = move blocks, .structured = o::some(move structured), .is_error = false};
}

/* R-SLIB-MCP-0007: a form that asks the user for values: a message and a flat JSON Schema of an
   object whose properties are strings, numbers, booleans or enums. */
struct form_request { std.string::string message; std.json::value schema; };

/* R-SLIB-MCP-0007: a URL that the user opens in a browser, with a message. */
struct url_request { std.string::string message; std.string::string url; };

/* R-SLIB-MCP-0007: a request for input from the user through the client. */
enum elicitation { form(form_request), url(url_request) };

/* R-SLIB-MCP-0007: the action of the user: accept (a form carries content), decline or cancel. */
@derive(format)
enum answer_action { accept, decline, cancel };

/* R-SLIB-MCP-0007: the answer of the client to one input request. */
struct answer { answer_action action; o<std.json::value> content; };

/* R-SLIB-MCP-0007: a field of the content of an accepted form, none when absent. */
o<const std.json::value*> answer::field(const answer* this, str name) {
    switch (this->content) {
    case variant o::some(value): return member(value, name);
    case variant o::none: return o::none;
    }
}

/* R-SLIB-MCP-0007: the input requests of a result that needs input, each under a key that the
   server chooses, and an opaque state that the client echoes with its answers. */
struct input_required {
    protected array<std.string::string> keys;
    protected array<elicitation> requests;
    o<std.string::string> state;
};

input_required input_required::create() {
    return input_required {.keys = std.array::create::<std.string::string>(),
                           .requests = std.array::create::<elicitation>(), .state = o::none};
}

/* R-SLIB-MCP-0007: asks for a form under a key. */
void input_required::ask_form(input_required* this, str key, str message, std.json::value schema)
    throws std.alloc::alloc_error {
    add_text(&this->keys, key);
    append(&this->requests, elicitation::form(form_request {.message = std.string::from_str(message),
                                                            .schema = move schema}));
}

/* R-SLIB-MCP-0007: asks the user to open a URL, under a key. */
void input_required::ask_url(input_required* this, str key, str message, str url) throws std.alloc::alloc_error {
    add_text(&this->keys, key);
    append(&this->requests, elicitation::url(url_request {.message = std.string::from_str(message),
                                                          .url = std.string::from_str(url)}));
}

/* R-SLIB-MCP-0007: sets the state that the client echoes. */
void input_required::set_state(input_required* this, str state) throws std.alloc::alloc_error {
    o<std.string::string> old = core::replace(&this->state, o::some(std.string::from_str(state)));
    drop old;
}

/* R-SLIB-MCP-0007: the number of input requests, and the key and the request at an index. */
usize input_required::count(const input_required* this) { return len(this->requests); }
str input_required::key_at(const input_required* this, usize index) { return this->keys[index]; }
const elicitation* input_required::request_at(const input_required* this, usize index) {
    return &this->requests[index];
}

/* R-SLIB-MCP-0005: the outcome of reading a resource: its contents, input needed first, or no
   resource at that URI. */
enum read_outcome { complete(array<contents>), needs_input(input_required), not_found };

/* R-SLIB-MCP-0006: the outcome of a tool call. */
enum tool_outcome { complete(tool_result), needs_input(input_required) };

/* R-SLIB-MCP-0008: an argument of a prompt. */
struct prompt_argument {
    std.string::string name;
    @json(optional, omitempty) std.string::string title;
    @json(optional, omitempty) std.string::string description;
    @json(optional, omitzero) bool required;
};

/* R-SLIB-MCP-0008: a prompt that a server offers. */
struct prompt {
    std.string::string name;
    @json(optional, omitempty) std.string::string title;
    @json(optional, omitempty) std.string::string description;
    @json(optional, omitempty) array<prompt_argument> arguments;
};

prompt prompt::create(str name, str description) throws std.alloc::alloc_error {
    return prompt {.name = std.string::from_str(name), .title = std.string::create(),
                   .description = std.string::from_str(description),
                   .arguments = std.array::create::<prompt_argument>()};
}

/* R-SLIB-MCP-0008: adds an argument to the prompt. */
void prompt::argument(prompt* this, str name, str description, bool required) throws std.alloc::alloc_error {
    append(&this->arguments, prompt_argument {.name = std.string::from_str(name), .title = std.string::create(),
                                              .description = std.string::from_str(description),
                                              .required = required});
}

/* R-SLIB-MCP-0008: the role of a message. */
@derive(format)
enum role { user, assistant };

/* R-SLIB-MCP-0008: one message of a prompt, with one block of content. */
struct prompt_message { role role; content content; };

/* R-SLIB-MCP-0008: the messages of a prompt with its arguments applied. */
struct prompt_result {
    @json(optional, omitempty) std.string::string description;
    array<prompt_message> messages;
};

prompt_result prompt_result::create(str description) throws std.alloc::alloc_error {
    return prompt_result {.description = std.string::from_str(description),
                          .messages = std.array::create::<prompt_message>()};
}

/* R-SLIB-MCP-0008: adds a message of one text block. */
void prompt_result::say(prompt_result* this, role speaker, str text) throws std.alloc::alloc_error {
    append(&this->messages, prompt_message {.role = speaker, .content = content::of_text(text)});
}

/* R-SLIB-MCP-0008: the outcome of getting a prompt. */
enum prompt_outcome { complete(prompt_result), needs_input(input_required) };

/* R-SLIB-MCP-0009: the completion of an argument: at most 100 values, the total when known and
   whether more exist. */
struct completion {
    array<std.string::string> values;
    @json(optional, omitnone) o<u64> total;
    @json(name = "hasMore", optional, omitzero) bool has_more;
};

completion completion::create() {
    return completion {.values = std.array::create::<std.string::string>(), .total = o::none,
                       .has_more = false};
}

/* ---- Server: changes and settings ---- */

/* R-SLIB-MCP-0010: the handle through which a program announces the changes that subscriptions
   of clients receive. A change travels as text: "t", "p" and "r" for the lists of tools, prompts
   and resources, "u" and a URI for an updated resource, and (R-SLIB-MCP-0023) "k" and the id of
   a task whose state changed. */
struct notifier { protected std.async::broadcast<std.string::string> sender; };

notifier notifier::create() throws std.alloc::alloc_error {
    return notifier {.sender = std.async::broadcast::<std.string::string>(64usize)};
}

/* R-SLIB-MCP-0010: another handle of the same notifier. */
notifier notifier::share(const notifier* this) {
    return notifier {.sender = std.async::clone_broadcast(&this->sender)};
}

protected void announce(const notifier* changes, str text) throws std.alloc::alloc_error {
    usize count = std.async::publish(&changes->sender, std.string::from_str(text));
    count as void;
}

/* R-SLIB-MCP-0010: the list of tools changed. */
void notifier::tools_changed(const notifier* this) throws std.alloc::alloc_error { announce(this, "t"); }

/* R-SLIB-MCP-0010: the list of prompts changed. */
void notifier::prompts_changed(const notifier* this) throws std.alloc::alloc_error { announce(this, "p"); }

/* R-SLIB-MCP-0010: the list of resources changed. */
void notifier::resources_changed(const notifier* this) throws std.alloc::alloc_error { announce(this, "r"); }

/* R-SLIB-MCP-0010: the resource at the URI changed. */
void notifier::resource_updated(const notifier* this, str uri) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("u");
    std.string::append_str(&text, uri);
    usize count = std.async::publish(&this->sender, move text);
    count as void;
}

/* R-SLIB-MCP-0011: the settings of a server: the freshness of discover, lists and reads in
   milliseconds and whether they may be shared between users; the page size of lists; whether
   subscriptions may ask for list changes and for resource updates; the longest message; and
   (R-SLIB-MCP-0022) the time a task is kept and the interval at which its client polls it. */
struct options {
    u64 ttl_ms = 300000u64;
    bool private_cache = false;
    usize page_size = 100usize;
    bool list_changed = false;
    bool subscribe = false;
    usize max_message = 4194304usize;
    u64 task_ttl_ms = 3600000u64;
    u64 task_poll_ms = 1000u64;
};

/* ---- Handler contexts ---- */

/* R-SLIB-MCP-0007: what a request carries for Multi Round-Trip Requests: the answers of the
   client under the keys of the input requests they answer, the state echoed from the previous
   round, and the elicitation modes that the client declared on this request. */
struct input {
    protected array<std.string::string> keys;
    protected array<answer> answers;
    o<std.string::string> state;
    bool form;
    bool url;
};

/* R-SLIB-MCP-0007: the answer under a key, none when the client sent none. */
o<const answer*> input::get(const input* this, str key) {
    for (usize index = 0usize; index < len(this->keys); index += 1usize) {
        if (std.bytes::equal(this->keys[index], key) == true) {
            return o::some(&this->answers[index]);
        }
    }
    return o::none;
}

/* R-SLIB-MCP-0007: the echoed state, empty when the client sent none. */
str input::state_text(const input* this) {
    switch (this->state) {
    case variant o::some(value): return *value;
    case variant o::none: return "";
    }
}

/* One message that a server sends for a request, a notification about it or its response, with
   the HTTP status of a response; a message of none keeps a stream alive. */
protected struct outgoing { o<std.jsonrpc::message> message; u16 status; };

/* ---- Tasks (the Tasks extension, R-SLIB-MCP-0021..0024) ---- */

/* R-SLIB-MCP-0021: the identifier of the Tasks extension among the extensions of the capabilities
   of a client and of a server. */
const constexpr str tasks_extension = "io.modelcontextprotocol/tasks";

/* R-SLIB-MCP-0021: the status of a task; completed, failed and cancelled are final. */
@derive(format)
enum task_status { working, input_required, completed, failed, cancelled };

/* A task of a server: its id and times, its status and message, the input requests of its
   current round with the answers that came, its result (completed) or error (failed), and the
   signals through which its runner learns of answers and of a cancellation. */
protected struct task_entry {
    std.string::string id;
    task_status status;
    std.string::string message;
    std.string::string created;
    std.string::string updated;
    o<std.time::instant> expires;
    u32 round;
    std.json::value requests;
    array<std.string::string> outstanding;
    array<std.string::string> keys;
    array<answer> answers;
    o<std.string::string> state;
    std.json::value outcome;
    std.async::notify wake;
    std.async::notify stop;
};

/* A call of a tool that runs as a task: the task, the tool and its arguments, and the
   elicitation modes that the client declared on the call. */
protected struct task_job {
    std.string::string id;
    usize tool;
    std.string::string name;
    std.json::value arguments;
    bool form;
    bool url;
};

/* What the runner of the tasks receives: a call to start, or the stop of the program. */
protected enum task_work { start(task_job), stop(std.service::stop) };

/* The tasks of a server, shared by its requests, its runner and the progress of the handlers
   that run as tasks: the entries, the channel of the runner (its receiver taken by run_tasks),
   whether a runner runs and whether it is closing, the settings of the tasks and a handle of
   the notifier of the server. */
protected struct task_board {
    std.sync::mutex<array<task_entry>> entries;
    std.sync::sender<task_work> jobs;
    std.sync::mutex<o<std.sync::receiver<task_work>>> inbox;
    atomic u32 running;
    atomic u32 closing;
    u64 ttl_ms;
    u64 poll_ms;
    notifier changes;
};

/* The task of a handler that runs as one, for its progress. */
protected struct task_link { arc task_board board; std.string::string id; };

/* ---- The board of the tasks (R-SLIB-MCP-0022) ---- */

/* The current time of the system in RFC 3339 with milliseconds. */
protected std.string::string time_text() throws std.alloc::alloc_error {
    try {
        std.time::system_time now = std.time::system_now();
        return std.time::format_rfc3339(now, 3u32);
    } catch (std.time::time_error rejected) {
        rejected as void;
    }
    return std.string::from_str("1970-01-01T00:00:00.000Z");
}

/* A duration of milliseconds. */
protected std.time::duration millis(u64 count) {
    try {
        return std.time::duration_from_parts((count / 1000u64) as i64, ((count % 1000u64) * 1000000u64) as u32);
    } catch (std.time::duration_error rejected) {
        rejected as void;
    }
    return std.time::duration_from_seconds(0i64);
}

/* The instant at which a task made now expires; none when the clock fails. */
protected o<std.time::instant> expiry(u64 ttl_ms) {
    try {
        std.time::instant now = std.time::monotonic_now();
        return o::some(std.time::instant_add(now, millis(ttl_ms)));
    } catch (std.time::time_error rejected) {
        rejected as void;
    }
    return o::none;
}

/* Whether an expiry lies at or before now. */
protected bool task_expired(const (o<std.time::instant>)* limit, std.time::instant now) {
    switch (*limit) {
    case variant o::some(at):
        try {
            std.time::duration past = std.time::instant_duration(now, *at);
            past as void;
            return true;
        } catch (std.time::time_error rejected) {
            rejected as void;
        }
    case variant o::none: break;
    }
    return false;
}

protected str status_name(task_status status) {
    switch (status) {
    case task_status::working: return "working";
    case task_status::input_required: return "input_required";
    case task_status::completed: return "completed";
    case task_status::failed: return "failed";
    case task_status::cancelled: return "cancelled";
    }
}

protected bool is_final(task_status status) {
    return status == task_status::completed || status == task_status::failed || status == task_status::cancelled;
}

/* The monotonic now; none when the clock fails. */
protected o<std.time::instant> instant_now() {
    try {
        return o::some(std.time::monotonic_now());
    } catch (std.time::time_error rejected) {
        rejected as void;
    }
    return o::none;
}

/* Removes the tasks whose time to live has passed; a task that still runs is stopped. */
protected void purge(array<task_entry>* entries) {
    switch (instant_now()) {
    case variant o::some(now):
        usize index = 0usize;
        while (index < len(*entries)) {
            if (task_expired(&(*entries)[index].expires, *now) == true) {
                (*entries)[index].stop.notify_one();
                o<task_entry> removed = entries->remove(index);
                drop removed;
            } else {
                index += 1usize;
            }
        }
    case variant o::none: break;
    }
}

protected o<usize> task_index(const array<task_entry>* entries, str id) {
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if (std.bytes::equal((*entries)[index].id, id) == true) { return o::some(index); }
    }
    return o::none;
}

/* The input requests of a task that still wait for their answers. */
protected std.json::value outstanding_requests(const task_entry* entry) throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    for (usize at = 0usize; at < len(entry->outstanding); at += 1usize) {
        str key = entry->outstanding[at];
        switch (std.json::find(&entry->requests, key)) {
        case variant o::some(request): put(&result, key, copy_value(*request));
        case variant o::none: break;
        }
    }
    return move result;
}

/* R-SLIB-MCP-0022: the JSON of a task with the fields of its status: the input requests that
   wait for answers, the result or the error. */
protected std.json::value detail_of(const task_entry* entry, u64 ttl_ms, u64 poll_ms)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    put_text(&result, "taskId", entry->id);
    put_text(&result, "status", status_name(entry->status));
    if (std.string::len(&entry->message) != 0usize) { put_text(&result, "statusMessage", entry->message); }
    put_text(&result, "createdAt", entry->created);
    put_text(&result, "lastUpdatedAt", entry->updated);
    put(&result, "ttlMs", json_unsigned(ttl_ms));
    put(&result, "pollIntervalMs", json_unsigned(poll_ms));
    if (entry->status == task_status::input_required) { put(&result, "inputRequests", outstanding_requests(entry)); }
    if (entry->status == task_status::completed) { put(&result, "result", copy_value(&entry->outcome)); }
    if (entry->status == task_status::failed) { put(&result, "error", copy_value(&entry->outcome)); }
    return move result;
}

/* Announces a change of a task to the subscriptions: "k" and its id. */
protected void announce_task(const task_board* board, str id) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("k");
    std.string::append_str(&text, id);
    announce(&board->changes, text);
}

/* Sets the message of a task that works; false when there is no such task or it does not work. */
protected bool note_in(array<task_entry>* entries, str id, str text) throws std.alloc::alloc_error {
    switch (task_index(entries, id)) {
    case variant o::some(index):
        task_entry* entry = &(*entries)[*index];
        if (entry->status != task_status::working) { return false; }
        std.string::clear(&entry->message);
        std.string::append_str(&entry->message, text);
        std.string::string now = time_text();
        std.string::string old = core::replace(&entry->updated, move now);
        drop old;
        return true;
    case variant o::none: return false;
    }
}

protected bool board_note(const task_board* board, str id, str text) throws std.alloc::alloc_error {
    bool noted = false;
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): noted = note_in(std.sync::mutex_guard_mut(&guard), id, text);
    case variant std.sync::lock_result::poisoned(move guard): noted = note_in(std.sync::mutex_guard_mut(&guard), id, text);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    if (noted == true) { announce_task(board, id); }
    return noted;
}

/* R-SLIB-MCP-0022: the methods of the Tasks extension. */
protected bool is_task_method(str method) {
    return std.bytes::equal(method, "tasks/get") == true || std.bytes::equal(method, "tasks/update") == true ||
           std.bytes::equal(method, "tasks/cancel") == true;
}

/* R-SLIB-MCP-0012: the progress of a request whose client asked for it with a progress token,
   or of a handler that runs as a task (R-SLIB-MCP-0022). */
struct progress {
    protected o<std.json::value> token;
    protected o<std.sync::sync_sender<outgoing>> sender;
    protected f64 last;
    protected bool started;
    protected o<task_link> link;
};

/* R-SLIB-MCP-0012: whether the client asked for progress, or the handler runs as a task. */
bool progress::wanted(const progress* this) {
    switch (this->link) {
    case variant o::some(link):
        link as void;
        return true;
    case variant o::none: break;
    }
    switch (this->token) {
    case variant o::some(value):
        value as void;
        return true;
    case variant o::none: return false;
    }
}

/* The status message of a report in a task: its message, otherwise the value and the total. */
protected std.string::string progress_text(f64 value, o<f64> total, str message) throws std.alloc::alloc_error {
    const u8[] given = message;
    if (len(given) != 0usize) { return std.string::from_str(message); }
    switch (total) {
    case variant o::some(amount):
        f64 whole = *amount;
        return f"{value}/{whole}";
    case variant o::none: break;
    }
    return f"{value}";
}

protected std.json::value progress_params(const std.json::value* token, f64 value, o<f64> total, str message)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value params = std.json::object();
    put(&params, "progressToken", copy_value(token));
    put(&params, "progress", json_float(value));
    switch (total) {
    case variant o::some(amount): put(&params, "total", json_float(*amount));
    case variant o::none: break;
    }
    const u8[] text = message;
    if (len(text) != 0usize) { put_text(&params, "message", message); }
    return move params;
}

/* R-SLIB-MCP-0012: reports that value of total (none when unknown) is done, with a message (empty
   for none). A report that does not increase the value, or that finds the stream of the request
   full, is dropped; false means that nothing was sent. */
bool progress::report(progress* this, f64 value, o<f64> total, str message) throws std.alloc::alloc_error {
    if (this->started == true && value <= this->last) { return false; }
    o<bool> noted = o::none;
    switch (this->link) {
    case variant o::some(link):
        std.string::string text = progress_text(value, total, message);
        noted = o::some(board_note(&*link->board, link->id, text));
    case variant o::none: break;
    }
    switch (noted) {
    case variant o::some(done):
        if (*done == true) {
            this->started = true;
            this->last = value;
        }
        return *done;
    case variant o::none: break;
    }
    o<std.json::value> built = o::none;
    switch (this->token) {
    case variant o::some(token):
        try {
            built = o::some(progress_params(token, value, total, message));
        } catch (std.json::error rejected) {
            (move rejected) as void;
        }
    case variant o::none: break;
    }
    bool delivered = false;
    switch (move built) {
    case variant o::some(move params):
        std.jsonrpc::notification note = {.method = std.string::from_str("notifications/progress"),
                                          .params = o::some(move params)};
        switch (this->sender) {
        case variant o::some(sender):
            outgoing item = {.message = o::some(std.jsonrpc::message::notification(move note)),
                             .status = 200u16};
            std.sync::try_send_result<outgoing> sent = std.sync::try_send(sender, move item);
            switch (move sent) {
            case variant std.sync::try_send_result::sent: delivered = true;
            case variant std.sync::try_send_result::full(move unsent): drop unsent;
            case variant std.sync::try_send_result::disconnected(move unsent): drop unsent;
            }
        case variant o::none: drop note;
        }
    case variant o::none: break;
    }
    if (delivered == true) {
        this->started = true;
        this->last = value;
    }
    return delivered;
}

/* R-SLIB-MCP-0006: a call of a tool as its handler receives it: the name of the tool and its
   arguments, an object. */
struct call {
    std.string::string name;
    std.json::value arguments;
    input input;
    progress progress;
};

/* R-SLIB-MCP-0006: the arguments as JSON text, which std.json::unmarshal reads into the type
   whose schema the tool declared. */
std.string::string call::arguments_text(const call* this) throws std.json::error, std.alloc::alloc_error {
    return std.json::stringify(&this->arguments);
}

/* R-SLIB-MCP-0005: a read of a resource as its handler receives it: the URI and, for a template,
   the values of its variables. */
struct read {
    std.string::string uri;
    protected array<std.string::string> names;
    protected array<std.string::string> values;
    input input;
    progress progress;
};

/* R-SLIB-MCP-0005: the value of a variable of the template, percent-decoded. */
o<str> read::variable(const read* this, str name) { return lookup(&this->names, &this->values, name); }

/* R-SLIB-MCP-0008: a request for a prompt as its handler receives it. */
struct prompt_call {
    std.string::string name;
    protected array<std.string::string> names;
    protected array<std.string::string> values;
    input input;
    progress progress;
};

/* R-SLIB-MCP-0008: the value of an argument, none when the client gave none. */
o<str> prompt_call::argument(const prompt_call* this, str name) {
    return lookup(&this->names, &this->values, name);
}

/* R-SLIB-MCP-0009: a request to complete an argument of a prompt (prompt is true, reference is
   its name) or a variable of a resource template (reference is the template), with the values of
   the other arguments that the client already knows. */
struct completion_request {
    bool prompt;
    std.string::string reference;
    std.string::string argument;
    std.string::string value;
    protected array<std.string::string> names;
    protected array<std.string::string> values;
};

/* R-SLIB-MCP-0009: a value that the client already knows, none when it sent none. */
o<str> completion_request::context(const completion_request* this, str name) {
    return lookup(&this->names, &this->values, name);
}

/* ---- Server ---- */

/* R-SLIB-MCP-0020: the decision about the bearer token of a request: allowed, not valid for
   this server, or valid without the scope that the method needs. */
@derive(format)
enum access { allowed, invalid, insufficient };

/* R-SLIB-MCP-0020: what a server protected by OAuth 2.1 announces as its Protected Resource
   Metadata (RFC 9728): its canonical URI, the authorization servers whose tokens it accepts,
   and its scopes, which its 401 challenges name. */
struct protection {
    std.string::string resource;
    array<std.string::string> authorization_servers;
    array<std.string::string> scopes;
};

protection protection::create(str resource, str authorization_server) throws std.alloc::alloc_error {
    array<std.string::string> servers = std.array::create::<std.string::string>();
    add_text(&servers, authorization_server);
    return protection {.resource = std.string::from_str(resource), .authorization_servers = move servers,
                       .scopes = std.array::create::<std.string::string>()};
}

/* R-SLIB-MCP-0020: adds a scope that the server knows. */
void protection::scope(protection* this, str name) throws std.alloc::alloc_error { add_text(&this->scopes, name); }

@generic<S: send & sync & unborrowed>
protected struct tool_entry {
    tool definition;
    async fn(arc S, call) -> tool_outcome throws(std.error::fault) handler;
    u8 task_mode;
};

@generic<S: send & sync & unborrowed>
protected struct resource_entry {
    resource definition;
    async fn(arc S, read) -> read_outcome throws(std.error::fault) handler;
};

@generic<S: send & sync & unborrowed>
protected struct template_entry {
    resource_template definition;
    async fn(arc S, read) -> read_outcome throws(std.error::fault) handler;
};

@generic<S: send & sync & unborrowed>
protected struct prompt_entry {
    prompt definition;
    async fn(arc S, prompt_call) -> prompt_outcome throws(std.error::fault) handler;
};

/* R-SLIB-MCP-0011: a server: its identity, instructions and settings, the shared state S of its
   handlers, the tools, resources, resource templates and prompts it offers with their handlers,
   and the notifier of its changes. */
@generic<S: send & sync & unborrowed>
struct server {
    arc S state;
    protected implementation info;
    protected std.string::string instructions;
    protected options settings;
    protected notifier changes;
    protected array<std.string::string> origins;
    protected array<tool_entry<S>> tools;
    protected array<resource_entry<S>> resources;
    protected array<template_entry<S>> templates;
    protected array<prompt_entry<S>> prompts;
    protected o<async fn(arc S) -> array<resource> throws(std.error::fault)> lister;
    protected o<async fn(arc S, completion_request) -> completion throws(std.error::fault)> completer;
    protected o<protection> guard;
    protected o<fn(const S*, str, str) -> access throws(std.alloc::alloc_error)> checker;
    protected o<arc task_board> board;
};

@generic<S: send & sync & unborrowed>
server<S> server<S>::create(implementation info, arc S state, notifier changes, options settings) {
    return server<S> {
        .state = move state, .info = move info, .instructions = std.string::create(),
        .settings = settings, .changes = move changes,
        .origins = std.array::create::<std.string::string>(),
        .tools = std.array::create::<tool_entry<S>>(),
        .resources = std.array::create::<resource_entry<S>>(),
        .templates = std.array::create::<template_entry<S>>(),
        .prompts = std.array::create::<prompt_entry<S>>(),
        .lister = o::none, .completer = o::none, .guard = o::none, .checker = o::none, .board = o::none};
}

/* R-SLIB-MCP-0011: natural-language guidance for the model that server/discover returns. */
@generic<S: send & sync & unborrowed>
void server<S>::set_instructions(server<S>* this, str text) throws std.alloc::alloc_error {
    std.string::string old = core::replace(&this->instructions, std.string::from_str(text));
    drop old;
}

/* Whether a name has 1 to 128 characters of A-Z, a-z, 0-9, '_', '-' and '.'. */
protected bool valid_name(str name) {
    const u8[] bytes = name;
    if (len(bytes) == 0usize || len(bytes) > 128usize) { return false; }
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 value = bytes[index];
        bool letter = (value >= 65u8 && value <= 90u8) || (value >= 97u8 && value <= 122u8);
        bool digit = value >= 48u8 && value <= 57u8;
        if (letter == false && digit == false && value != 95u8 && value != 45u8 && value != 46u8) {
            return false;
        }
    }
    return true;
}

/* Whether a JSON Schema is an object whose "type" is "object". */
protected bool object_schema(const std.json::value* schema) {
    switch (member_text(schema, "type")) {
    case variant o::some(kind): return std.bytes::equal(*kind, "object");
    case variant o::none: return false;
    }
}

/* R-SLIB-MCP-0011: adds a tool. Its name shall be valid and not taken, and its input schema an
   object schema. */
@generic<S: send & sync & unborrowed>
void server<S>::add_tool(server<S>* this, tool definition,
                         async fn(arc S, call) -> tool_outcome throws(std.error::fault) handler)
    throws mcp_error, std.alloc::alloc_error {
    throw (valid_name(definition.name) == false) failure(std.jsonrpc::invalid_params, "invalid tool name");
    throw (object_schema(&definition.input_schema) == false)
        failure(std.jsonrpc::invalid_params, "the input schema of a tool describes an object");
    for (usize index = 0usize; index < len(this->tools); index += 1usize) {
        throw (std.bytes::equal(this->tools[index].definition.name, definition.name) == true)
            failure(std.jsonrpc::invalid_params, "a tool of that name exists");
    }
    append(&this->tools, tool_entry<S> {.definition = move definition, .handler = handler, .task_mode = 0u8});
}

/* R-SLIB-MCP-0011: adds a resource. Its URI shall not be taken. */
@generic<S: send & sync & unborrowed>
void server<S>::add_resource(server<S>* this, resource definition,
                             async fn(arc S, read) -> read_outcome throws(std.error::fault) handler)
    throws mcp_error, std.alloc::alloc_error {
    throw (std.string::len(&definition.uri) == 0usize) failure(std.jsonrpc::invalid_params, "a resource has a URI");
    for (usize index = 0usize; index < len(this->resources); index += 1usize) {
        throw (std.bytes::equal(this->resources[index].definition.uri, definition.uri) == true)
            failure(std.jsonrpc::invalid_params, "a resource at that URI exists");
    }
    append(&this->resources, resource_entry<S> {.definition = move definition, .handler = handler});
}

/* The index of the '}' that ends a variable starting with '{' at start. */
protected o<usize> variable_end(const u8[] shape, usize start) {
    usize at = start + 1usize;
    while (at < len(shape)) {
        if (shape[at] == 125u8) { return o::some(at); }
        if (shape[at] == 123u8) { return o::none; }
        at += 1usize;
    }
    return o::none;
}

/* Whether a URI template has only literal text, {name} and {+name} variables with names of
   letters, digits and '_', and no two variables next to each other. */
protected bool valid_template(str text) {
    const u8[] shape = text;
    if (len(shape) == 0usize) { return false; }
    usize at = 0usize;
    bool after_variable = false;
    while (at < len(shape)) {
        if (shape[at] == 125u8) { return false; }
        if (shape[at] != 123u8) {
            at += 1usize;
            after_variable = false;
            continue;
        }
        if (after_variable == true) { return false; }
        usize end = 0usize;
        switch (variable_end(shape, at)) {
        case variant o::some(found): end = *found;
        case variant o::none: return false;
        }
        usize first = at + 1usize;
        if (first < end && shape[first] == 43u8) { first += 1usize; }
        if (first == end) { return false; }
        for (usize index = first; index < end; index += 1usize) {
            u8 value = shape[index];
            bool letter = (value >= 65u8 && value <= 90u8) || (value >= 97u8 && value <= 122u8);
            bool digit = value >= 48u8 && value <= 57u8;
            if (letter == false && digit == false && value != 95u8) { return false; }
        }
        at = end + 1usize;
        after_variable = true;
    }
    return true;
}

/* R-SLIB-MCP-0011: adds a resource template; its URI template shall be valid. */
@generic<S: send & sync & unborrowed>
void server<S>::add_template(server<S>* this, resource_template definition,
                             async fn(arc S, read) -> read_outcome throws(std.error::fault) handler)
    throws mcp_error, std.alloc::alloc_error {
    throw (valid_template(definition.uri_template) == false)
        failure(std.jsonrpc::invalid_params, "invalid URI template");
    append(&this->templates, template_entry<S> {.definition = move definition, .handler = handler});
}

/* R-SLIB-MCP-0011: adds a prompt. Its name shall be valid and not taken. */
@generic<S: send & sync & unborrowed>
void server<S>::add_prompt(server<S>* this, prompt definition,
                           async fn(arc S, prompt_call) -> prompt_outcome throws(std.error::fault) handler)
    throws mcp_error, std.alloc::alloc_error {
    throw (valid_name(definition.name) == false) failure(std.jsonrpc::invalid_params, "invalid prompt name");
    for (usize index = 0usize; index < len(this->prompts); index += 1usize) {
        throw (std.bytes::equal(this->prompts[index].definition.name, definition.name) == true)
            failure(std.jsonrpc::invalid_params, "a prompt of that name exists");
    }
    append(&this->prompts, prompt_entry<S> {.definition = move definition, .handler = handler});
}

/* R-SLIB-MCP-0011: lists resources beyond those added, such as the entries of a collection that
   a template reads; resources/list returns the added ones first. */
@generic<S: send & sync & unborrowed>
void server<S>::list_resources_with(server<S>* this,
                                    async fn(arc S) -> array<resource> throws(std.error::fault) lister) {
    this->lister = o::some(lister);
}

/* R-SLIB-MCP-0011: completes arguments of prompts and variables of templates. */
@generic<S: send & sync & unborrowed>
void server<S>::complete_with(server<S>* this,
                              async fn(arc S, completion_request) -> completion throws(std.error::fault) completer) {
    this->completer = o::some(completer);
}

/* R-SLIB-MCP-0011: allows browser requests from an origin, such as "https://app.example", over
   HTTP; requests from the origin of the server and from localhost are always allowed. */
@generic<S: send & sync & unborrowed>
void server<S>::allow_origin(server<S>* this, str origin) throws std.alloc::alloc_error {
    add_text(&this->origins, origin);
}

/* Whether the server has tasks: a tool that runs as one. */
@generic<S: send & sync & unborrowed>
protected bool has_board(const server<S>* host) {
    switch (host->board) {
    case variant o::some(board):
        board as void;
        return true;
    case variant o::none: return false;
    }
}

/* Another handle of the board of the tasks of a server, none without one. */
@generic<S: send & sync & unborrowed>
protected o<arc task_board> shared_board(const server<S>* host) {
    switch (host->board) {
    case variant o::some(board): return o::some(std.arc::clone(board));
    case variant o::none: return o::none;
    }
}

/* Whether a runner runs the tasks of the board of a server. */
@generic<S: send & sync & unborrowed>
protected bool runner_attached(const server<S>* host) {
    switch (host->board) {
    case variant o::some(board): return core::atomic_load(&(*board)->running, core::memory_order::acquire) == 1u32;
    case variant o::none: return false;
    }
}

/* The board of the tasks of a server, made when the server first runs a tool as a task. */
protected task_board new_board(const options* settings, notifier changes) throws std.alloc::alloc_error {
    std.sync::channel<task_work> factory = std.sync::channel::<task_work>();
    std.sync::sender<task_work> jobs = std.sync::sender(&factory);
    std.sync::receiver<task_work> inbox = std.sync::receiver(move factory);
    return task_board {.entries = std.sync::mutex_new(std.array::create::<task_entry>()), .jobs = move jobs,
                       .inbox = std.sync::mutex_new(o::some(move inbox)), .running = 0u32, .closing = 0u32,
                       .ttl_ms = settings->task_ttl_ms, .poll_ms = settings->task_poll_ms, .changes = move changes};
}

/* R-SLIB-MCP-0021: runs the calls of a tool as tasks for the clients that declare the Tasks
   extension while a runner runs (run_tasks); with required, a client that does not declare it
   gets -32021. The tool shall have been added. */
@generic<S: send & sync & unborrowed>
void server<S>::run_as_task(server<S>* this, str tool, bool required) throws mcp_error, std.alloc::alloc_error {
    bool found = false;
    for (usize index = 0usize; index < len(this->tools); index += 1usize) {
        if (std.bytes::equal(this->tools[index].definition.name, tool) == true) {
            this->tools[index].task_mode = 1u8;
            if (required == true) { this->tools[index].task_mode = 2u8; }
            found = true;
        }
    }
    throw (found == false) failure(std.jsonrpc::invalid_params, "no tool of that name");
    if (has_board(this) == false) {
        arc task_board made = new arc task_board(new_board(&this->settings, this->changes.share()));
        o<arc task_board> old = core::replace(&this->board, o::some(move made));
        drop old;
    }
}

/* R-SLIB-MCP-0020: protects the endpoint over HTTP: each POST carries a bearer token in its
   Authorization field, which check decides on for the method of the message (it shall verify
   that the token was issued for the resource of the protection). A POST without a valid token
   gets 401 and one whose token lacks a scope 403, both with a WWW-Authenticate challenge that
   names the metadata; route also serves that metadata. */
@generic<S: send & sync & unborrowed>
void server<S>::protect(server<S>* this, protection settings, fn(const S*, str, str) -> access throws(std.alloc::alloc_error) check) {
    o<protection> old = core::replace(&this->guard, o::some(move settings));
    drop old;
    this->checker = o::some(check);
}

/* R-SLIB-MCP-0010: a handle of the notifier of the server. */
@generic<S: send & sync & unborrowed>
notifier server<S>::notifier(const server<S>* this) { return this->changes.share(); }

@generic<S: send & sync & unborrowed>
protected bool has_resources(const server<S>* host) {
    if (len(host->resources) != 0usize || len(host->templates) != 0usize) { return true; }
    switch (host->lister) {
    case variant o::some(lister):
        lister as void;
        return true;
    case variant o::none: return false;
    }
}

@generic<S: send & sync & unborrowed>
protected bool has_completion(const server<S>* host) {
    switch (host->completer) {
    case variant o::some(completer):
        completer as void;
        return true;
    case variant o::none: return false;
    }
}

/* ---- Checks of a request ---- */

/* A request that passed the checks shared by every method: its id, method and parameters, and
   the elicitation modes and progress token of its _meta. */
protected struct admitted {
    std.jsonrpc::request_id id;
    std.string::string method;
    std.json::value params;
    bool form;
    bool url;
    o<std.json::value> token;
    bool tasks;
};

protected bool is_method(const admitted* request, str name) {
    return std.bytes::equal(request->method, name);
}

/* A refusal of a request: its JSON-RPC error and the HTTP status that carries it. */
protected struct refusal {
    i64 code;
    std.string::string message;
    o<std.json::value> data;
    u16 status;
};

protected refusal refusal_of(i64 code, str message, u16 status) throws std.alloc::alloc_error {
    return refusal {.code = code, .message = std.string::from_str(message), .data = o::none, .status = status};
}

protected enum admission { accepted(admitted), refused(refusal) };

/* What the _meta of a request says. */
protected struct request_meta {
    bool valid;
    std.string::string version;
    bool form;
    bool url;
    o<std.json::value> token;
    bool level;
    bool tasks;
};

/* R-SLIB-MCP-0021: whether capabilities declare an extension: an object under its identifier
   among their extensions. */
protected bool declares_extension(const std.json::value* declared, str name) {
    switch (member(declared, "extensions")) {
    case variant o::some(extensions):
        if (std.json::kind(*extensions) != std.json::value_kind::object) { return false; }
        return member_is(*extensions, name, std.json::value_kind::object);
    case variant o::none: return false;
    }
}

/* Whether the capabilities declare an elicitation mode; {} declares form. */
protected bool declares(const std.json::value* declared, str mode) {
    switch (member(declared, "elicitation")) {
    case variant o::some(modes):
        if (std.json::kind(*modes) != std.json::value_kind::object) { return false; }
        if (has_member(*modes, "form") == false && has_member(*modes, "url") == false) {
            return std.bytes::equal(mode, "form");
        }
        return has_member(*modes, mode);
    case variant o::none: break;
    }
    return false;
}

/* Whether a log level is one of those of RFC 5424 that MCP names. */
protected bool valid_level(str level) {
    return std.bytes::equal(level, "debug") == true || std.bytes::equal(level, "info") == true ||
           std.bytes::equal(level, "notice") == true || std.bytes::equal(level, "warning") == true ||
           std.bytes::equal(level, "error") == true || std.bytes::equal(level, "critical") == true ||
           std.bytes::equal(level, "alert") == true || std.bytes::equal(level, "emergency") == true;
}

protected o<std.json::value> token_of(const std.json::value* meta) throws std.json::error, std.alloc::alloc_error {
    switch (member(meta, "progressToken")) {
    case variant o::some(value):
        std.json::value_kind kind = std.json::kind(*value);
        if (kind == std.json::value_kind::string || kind == std.json::value_kind::number) {
            return o::some(copy_value(*value));
        }
    case variant o::none: break;
    }
    return o::none;
}

protected bool level_of(const std.json::value* meta) {
    switch (member(meta, key_level)) {
    case variant o::some(value):
        return std.json::kind(*value) == std.json::value_kind::string && valid_level(std.json::text(*value)) == true;
    case variant o::none: return true;
    }
}

protected request_meta meta_of(const std.json::value* params) throws std.json::error, std.alloc::alloc_error {
    switch (member(params, "_meta")) {
    case variant o::some(meta):
        bool valid = member_is(*meta, key_version, std.json::value_kind::string) == true &&
                     member_is(*meta, key_capabilities, std.json::value_kind::object) == true;
        bool form = false;
        bool url = false;
        bool tasks = false;
        switch (member(*meta, key_capabilities)) {
        case variant o::some(declared):
            form = declares(*declared, "form");
            url = declares(*declared, "url");
            tasks = declares_extension(*declared, tasks_extension);
        case variant o::none: break;
        }
        return request_meta {.valid = valid, .version = owned(member_text(*meta, key_version)),
                             .form = form, .url = url, .token = token_of(*meta), .level = level_of(*meta),
                             .tasks = tasks};
    case variant o::none: break;
    }
    return request_meta {.valid = false, .version = std.string::create(), .form = false, .url = false,
                         .token = o::none, .level = true, .tasks = false};
}

/* Whether the server offers a method: discover and listen always, the others with the features
   that serve them. */
@generic<S: send & sync & unborrowed>
protected bool offers(const server<S>* host, str method) {
    if (std.bytes::equal(method, "server/discover") == true ||
        std.bytes::equal(method, "subscriptions/listen") == true) {
        return true;
    }
    if (std.bytes::equal(method, "tools/list") == true || std.bytes::equal(method, "tools/call") == true) {
        return len(host->tools) != 0usize;
    }
    if (std.bytes::equal(method, "resources/list") == true ||
        std.bytes::equal(method, "resources/templates/list") == true ||
        std.bytes::equal(method, "resources/read") == true) {
        return has_resources(host);
    }
    if (std.bytes::equal(method, "prompts/list") == true || std.bytes::equal(method, "prompts/get") == true) {
        return len(host->prompts) != 0usize;
    }
    if (std.bytes::equal(method, "completion/complete") == true) { return has_completion(host); }
    if (std.bytes::equal(method, "tasks/get") == true || std.bytes::equal(method, "tasks/update") == true ||
        std.bytes::equal(method, "tasks/cancel") == true) {
        return has_board(host);
    }
    return false;
}

protected refusal unsupported(str requested) throws std.json::error, std.alloc::alloc_error {
    std.json::value data = std.json::object();
    std.json::value supported = std.json::array();
    std.json::append(&supported, std.json::from_string(protocol_version));
    put(&data, "supported", move supported);
    put_text(&data, "requested", requested);
    return refusal {.code = unsupported_protocol_version,
                    .message = std.string::from_str("Unsupported protocol version"),
                    .data = o::some(move data), .status = 400u16};
}

/* The checks of a request that every transport makes: _meta with a protocol version and client
   capabilities (-32602), the version of a header when the transport has one (-32020), the
   version (-32022), the method (-32601) and the log level (-32602). */
@generic<S: send & sync & unborrowed>
protected admission admit(const server<S>* host, std.jsonrpc::request message, o<str> header_version)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value params = value_or_null(core::replace(&message.params, o::none));
    request_meta meta = meta_of(&params);
    if (meta.valid == false) {
        return admission::refused(refusal_of(std.jsonrpc::invalid_params,
            "Invalid params: _meta requires io.modelcontextprotocol/protocolVersion and io.modelcontextprotocol/clientCapabilities",
            400u16));
    }
    switch (header_version) {
    case variant o::some(sent):
        if (std.bytes::equal(*sent, meta.version) == false) {
            return admission::refused(refusal_of(header_mismatch,
                "Header mismatch: MCP-Protocol-Version does not match the protocol version of the body", 400u16));
        }
    case variant o::none: break;
    }
    if (std.bytes::equal(meta.version, protocol_version) == false) {
        return admission::refused(unsupported(meta.version));
    }
    if (offers(host, message.method) == false) {
        return admission::refused(refusal_of(std.jsonrpc::method_not_found, "Method not found", 404u16));
    }
    if (meta.level == false) {
        return admission::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: logLevel", 400u16));
    }
    std.jsonrpc::request_id id = message.id.copy();
    std.string::string method = core::replace(&message.method, std.string::create());
    o<std.json::value> token = core::replace(&meta.token, o::none);
    return admission::accepted(admitted {.id = move id, .method = move method, .params = move params,
                                         .form = meta.form, .url = meta.url, .token = move token,
                                         .tasks = meta.tasks});
}

/* ---- Responses ---- */

protected outgoing success(const std.jsonrpc::request_id* id, std.json::value result) throws std.alloc::alloc_error {
    std.jsonrpc::result_response reply = {.id = id->copy(), .result = move result};
    return outgoing {.message = o::some(std.jsonrpc::message::result(move reply)), .status = 200u16};
}

protected outgoing refused_reply(o<std.jsonrpc::request_id> id, refusal problem) throws std.alloc::alloc_error {
    std.string::string text = core::replace(&problem.message, std.string::create());
    o<std.json::value> data = core::replace(&problem.data, o::none);
    std.jsonrpc::error_response reply = {.id = move id, .code = problem.code, .message = move text,
                                         .data = move data};
    return outgoing {.message = o::some(std.jsonrpc::message::failure(move reply)), .status = problem.status};
}

protected outgoing refuse(const std.jsonrpc::request_id* id, refusal problem) throws std.alloc::alloc_error {
    return refused_reply(o::some(id->copy()), move problem);
}

protected outgoing internal_error(const std.jsonrpc::request_id* id) throws std.alloc::alloc_error {
    return refuse(id, refusal_of(std.jsonrpc::internal_error, "Internal error", 200u16));
}

protected outgoing invalid_params(const std.jsonrpc::request_id* id, str message) throws std.alloc::alloc_error {
    return refuse(id, refusal_of(std.jsonrpc::invalid_params, message, 200u16));
}

/* Adds resultType and _meta with serverInfo to a result. */
protected void finish(std.json::value* result, const implementation* info, str kind)
    throws std.json::error, std.alloc::alloc_error {
    put_text(result, "resultType", kind);
    std.json::value meta = std.json::object();
    put(&meta, key_server, to_value(info));
    put(result, "_meta", move meta);
}

protected void cache_hints(std.json::value* result, const options* settings)
    throws std.json::error, std.alloc::alloc_error {
    put(result, "ttlMs", json_unsigned(settings->ttl_ms));
    if (settings->private_cache == true) {
        put_text(result, "cacheScope", "private");
    } else {
        put_text(result, "cacheScope", "public");
    }
}

/* The capabilities of a server: the features it has, with listChanged and subscribe when its
   settings allow them. */
@generic<S: send & sync & unborrowed>
protected std.json::value offered(const server<S>* host) throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    if (len(host->tools) != 0usize) {
        std.json::value tools = std.json::object();
        if (host->settings.list_changed == true) { put_flag(&tools, "listChanged"); }
        put(&result, "tools", move tools);
    }
    if (has_resources(host) == true) {
        std.json::value resources = std.json::object();
        if (host->settings.subscribe == true) { put_flag(&resources, "subscribe"); }
        if (host->settings.list_changed == true) { put_flag(&resources, "listChanged"); }
        put(&result, "resources", move resources);
    }
    if (len(host->prompts) != 0usize) {
        std.json::value prompts = std.json::object();
        if (host->settings.list_changed == true) { put_flag(&prompts, "listChanged"); }
        put(&result, "prompts", move prompts);
    }
    if (has_completion(host) == true) { put(&result, "completions", std.json::object()); }
    if (has_board(host) == true) {
        std.json::value extensions = std.json::object();
        put(&extensions, tasks_extension, std.json::object());
        put(&result, "extensions", move extensions);
    }
    return move result;
}

/* R-SLIB-MCP-0013: the result of server/discover. */
@generic<S: send & sync & unborrowed>
protected std.json::value discovery(const server<S>* host) throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    std.json::value versions = std.json::array();
    std.json::append(&versions, std.json::from_string(protocol_version));
    put(&result, "supportedVersions", move versions);
    put(&result, "capabilities", offered(host));
    if (std.string::len(&host->instructions) != 0usize) {
        put_text(&result, "instructions", host->instructions);
    }
    cache_hints(&result, &host->settings);
    finish(&result, &host->info, "complete");
    return move result;
}

/* The first index of the page that the cursor of a request selects: 0 without a cursor, none
   for a cursor that is not an index of the list. */
protected o<usize> page_start(const std.json::value* params, usize count) {
    switch (member(params, "cursor")) {
    case variant o::none: return o::some(0usize);
    case variant o::some(cursor):
        if (std.json::kind(*cursor) != std.json::value_kind::string) { return o::none; }
        try {
            usize start = std.convert::parse_usize(std.json::text(*cursor), 10u32);
            if (start <= count) { return o::some(start); }
        } catch (std.convert::parse_error rejected) {
            rejected as void;
        }
        return o::none;
    }
}

/* Adds the list under key, nextCursor when more entries follow, the cache hints and the rest. */
protected void finish_page(std.json::value* result, str key, std.json::value entries, usize next, usize count,
                           const implementation* info, const options* settings)
    throws std.json::error, std.alloc::alloc_error {
    put(result, key, move entries);
    if (next < count) {
        std.string::string cursor = f"{next}";
        put_text(result, "nextCursor", cursor);
    }
    cache_hints(result, settings);
    finish(result, info, "complete");
}

protected usize page_end(usize start, usize count, usize size) {
    usize step = size;
    if (step == 0usize) { step = 1usize; }
    if (count - start < step) { return count; }
    return start + step;
}

/* The responses of discover and of the lists of tools, templates and prompts. */
@generic<S: send & sync & unborrowed>
protected outgoing listing(const server<S>* host, const admitted* request) throws std.json::error, std.alloc::alloc_error {
    if (is_method(request, "server/discover") == true) { return success(&request->id, discovery(host)); }
    usize count = len(host->templates);
    if (is_method(request, "tools/list") == true) { count = len(host->tools); }
    if (is_method(request, "prompts/list") == true) { count = len(host->prompts); }
    usize start = 0usize;
    switch (page_start(&request->params, count)) {
    case variant o::some(first): start = *first;
    case variant o::none: return invalid_params(&request->id, "Invalid cursor");
    }
    usize end = page_end(start, count, host->settings.page_size);
    std.json::value entries = std.json::array();
    std.json::value result = std.json::object();
    if (is_method(request, "tools/list") == true) {
        for (usize index = start; index < end; index += 1usize) {
            std.json::append(&entries, to_value(&host->tools[index].definition));
        }
        finish_page(&result, "tools", move entries, end, count, &host->info, &host->settings);
        return success(&request->id, move result);
    }
    if (is_method(request, "prompts/list") == true) {
        for (usize index = start; index < end; index += 1usize) {
            std.json::append(&entries, to_value(&host->prompts[index].definition));
        }
        finish_page(&result, "prompts", move entries, end, count, &host->info, &host->settings);
        return success(&request->id, move result);
    }
    for (usize index = start; index < end; index += 1usize) {
        std.json::append(&entries, to_value(&host->templates[index].definition));
    }
    finish_page(&result, "resourceTemplates", move entries, end, count, &host->info, &host->settings);
    return success(&request->id, move result);
}

/* The piece of text between two byte indices at ASCII positions. */
protected str piece(str text, usize start, usize end) {
    const u8[] bytes = text;
    try {
        return core::validate_utf8(bytes[start..end]);
    } catch (core::utf8_error rejected) {
        rejected as void;
        return "";
    }
}

/* The response of resources/list: the added resources, then those of the lister. */
@generic<S: send & sync & unborrowed>
protected outgoing resource_page(const server<S>* host, const admitted* request, const array<resource>* listed)
    throws std.json::error, std.alloc::alloc_error {
    usize added = len(host->resources);
    usize count = added + len(*listed);
    usize start = 0usize;
    switch (page_start(&request->params, count)) {
    case variant o::some(first): start = *first;
    case variant o::none: return invalid_params(&request->id, "Invalid cursor");
    }
    usize end = page_end(start, count, host->settings.page_size);
    std.json::value entries = std.json::array();
    for (usize index = start; index < end; index += 1usize) {
        if (index < added) {
            std.json::append(&entries, to_value(&host->resources[index].definition));
        } else {
            std.json::append(&entries, to_value(&(*listed)[index - added]));
        }
    }
    std.json::value result = std.json::object();
    finish_page(&result, "resources", move entries, end, count, &host->info, &host->settings);
    return success(&request->id, move result);
}

@generic<S: send & sync & unborrowed>
protected async outgoing list_resources(arc server<S> host, admitted request) throws std.error::fault {
    array<resource> listed = std.array::create::<resource>();
    auto lister = host->lister;
    switch (lister) {
    case variant o::some(chosen):
        auto list_all = *chosen;
        try {
            array<resource> found = await list_all(std.arc::clone(&host->state));
            array<resource> old = core::replace(&listed, move found);
            drop old;
        } catch (std.error::fault rejected) {
            rejected as void;
            return internal_error(&request.id);
        }
    case variant o::none: break;
    }
    try {
        return resource_page(&*host, &request, &listed);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return internal_error(&request.id);
}

/* The answer of one entry of inputResponses; none when it is no answer of elicitation, and an
   error when its action is unknown or its content is no object. */
protected o<answer> answer_of(const std.json::value* entry) throws std.json::error, std.alloc::alloc_error {
    answer_action chosen = answer_action::cancel;
    switch (member_text(entry, "action")) {
    case variant o::some(action):
        if (std.bytes::equal(*action, "accept") == true) {
            chosen = answer_action::accept;
        } else {
            if (std.bytes::equal(*action, "decline") == true) {
                chosen = answer_action::decline;
            } else {
                throw (std.bytes::equal(*action, "cancel") == false) shape_error();
            }
        }
    case variant o::none: return o::none;
    }
    switch (member(entry, "content")) {
    case variant o::some(value):
        throw (std.json::kind(*value) != std.json::value_kind::object) shape_error();
        return o::some(answer {.action = chosen, .content = o::some(copy_value(*value))});
    case variant o::none: break;
    }
    return o::some(answer {.action = chosen, .content = o::none});
}

/* The input of a request, its inputResponses and requestState; an error when they are
   malformed. Entries that are no answers of elicitation are ignored. */
protected input input_of(const std.json::value* params, bool form, bool url)
    throws std.json::error, std.alloc::alloc_error {
    input result = {.keys = std.array::create::<std.string::string>(),
                    .answers = std.array::create::<answer>(), .state = o::none, .form = form, .url = url};
    switch (member(params, "requestState")) {
    case variant o::some(value):
        throw (std.json::kind(*value) != std.json::value_kind::string) shape_error();
        result.state = o::some(std.string::from_str(std.json::text(*value)));
    case variant o::none: break;
    }
    switch (member(params, "inputResponses")) {
    case variant o::some(responses):
        throw (std.json::kind(*responses) != std.json::value_kind::object) shape_error();
        for (usize index = 0usize; index < std.json::len(*responses); index += 1usize) {
            str key = std.json::key_at(*responses, index);
            switch (std.json::find(*responses, key)) {
            case variant o::some(entry):
                o<answer> given = answer_of(*entry);
                switch (move given) {
                case variant o::some(move value):
                    add_text(&result.keys, key);
                    append(&result.answers, move value);
                case variant o::none: break;
                }
            case variant o::none: break;
            }
        }
    case variant o::none: break;
    }
    return move result;
}

protected progress progress_of(o<std.json::value> token, const (std.sync::sync_sender<outgoing>)* sender) {
    switch (move token) {
    case variant o::some(move value):
        return progress {.token = o::some(move value), .sender = o::some(std.sync::clone_sync_sender(sender)),
                         .last = 0.0f64, .started = false, .link = o::none};
    case variant o::none: break;
    }
    return progress {.token = o::none, .sender = o::none, .last = 0.0f64, .started = false, .link = o::none};
}

/* The data of -32021 for the modes that the requests need and the client did not declare. */
protected refusal missing_modes(bool form, bool url) throws std.json::error, std.alloc::alloc_error {
    std.json::value modes = std.json::object();
    if (form == true) { put(&modes, "form", std.json::object()); }
    if (url == true) { put(&modes, "url", std.json::object()); }
    std.json::value required = std.json::object();
    put(&required, "elicitation", move modes);
    std.json::value data = std.json::object();
    put(&data, "requiredCapabilities", move required);
    return refusal {.code = missing_required_client_capability,
                    .message = std.string::from_str("Server requires the elicitation capability for this request"),
                    .data = o::some(move data), .status = 400u16};
}

protected std.json::value elicitation_value(const elicitation* request) throws std.json::error, std.alloc::alloc_error {
    std.json::value params = std.json::object();
    switch (*request) {
    case variant elicitation::form(asked):
        put_text(&params, "mode", "form");
        put_text(&params, "message", asked->message);
        put(&params, "requestedSchema", copy_value(&asked->schema));
    case variant elicitation::url(asked):
        put_text(&params, "mode", "url");
        put_text(&params, "message", asked->message);
        put_text(&params, "url", asked->url);
    }
    std.json::value object = std.json::object();
    put_text(&object, "method", "elicitation/create");
    put(&object, "params", move params);
    return move object;
}

/* The response of a request whose handler needs input: -32021 when the client did not declare a
   mode that a request needs, otherwise the InputRequiredResult. */
protected outgoing input_reply(const implementation* info, const admitted* request, const input_required* asked)
    throws std.json::error, std.alloc::alloc_error {
    bool form = false;
    bool url = false;
    for (usize index = 0usize; index < len(asked->requests); index += 1usize) {
        switch (asked->requests[index]) {
        case variant elicitation::form(item):
            item as void;
            form = true;
        case variant elicitation::url(item):
            item as void;
            url = true;
        }
    }
    bool missing_form = form == true && request->form == false;
    bool missing_url = url == true && request->url == false;
    if (missing_form == true || missing_url == true) {
        return refuse(&request->id, missing_modes(missing_form, missing_url));
    }
    bool stated = false;
    switch (asked->state) {
    case variant o::some(state):
        state as void;
        stated = true;
    case variant o::none: break;
    }
    if (stated == false && len(asked->requests) == 0usize) { return internal_error(&request->id); }
    std.json::value result = std.json::object();
    if (len(asked->requests) != 0usize) {
        std.json::value requests = std.json::object();
        for (usize index = 0usize; index < len(asked->requests); index += 1usize) {
            put(&requests, asked->keys[index], elicitation_value(&asked->requests[index]));
        }
        put(&result, "inputRequests", move requests);
    }
    switch (asked->state) {
    case variant o::some(state): put_text(&result, "requestState", *state);
    case variant o::none: break;
    }
    finish(&result, info, "input_required");
    return success(&request->id, move result);
}

protected void put_entry(array<task_entry>* entries, task_entry entry) throws std.alloc::alloc_error {
    try {
        entries->push(move entry);
    } catch (std.array::push_error<task_entry> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* A new task that works: the JSON of its first state, none when the board is locked out. */
protected o<std.json::value> board_create(const task_board* board, str id)
    throws std.json::error, std.alloc::alloc_error {
    std.string::string now = time_text();
    task_entry entry = {.id = std.string::from_str(id), .status = task_status::working, .message = std.string::create(),
                        .created = std.string::from_str(now), .updated = move now,
                        .expires = expiry(board->ttl_ms), .round = 0u32, .requests = std.json::null(),
                        .outstanding = std.array::create::<std.string::string>(),
                        .keys = std.array::create::<std.string::string>(), .answers = std.array::create::<answer>(),
                        .state = o::none, .outcome = std.json::null(), .wake = std.async::notify_new(),
                        .stop = std.async::notify_new()};
    std.json::value seed = detail_of(&entry, board->ttl_ms, board->poll_ms);
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        array<task_entry>* entries = std.sync::mutex_guard_mut(&guard);
        purge(entries);
        put_entry(entries, move entry);
        return o::some(move seed);
    case variant std.sync::lock_result::poisoned(move guard):
        array<task_entry>* entries = std.sync::mutex_guard_mut(&guard);
        purge(entries);
        put_entry(entries, move entry);
        return o::some(move seed);
    case variant std.sync::lock_result::would_deadlock:
        drop seed;
        drop entry;
    }
    return o::none;
}

/* The JSON of a task as tasks/get answers it; none when there is no such task. */
protected o<std.json::value> detail_in(array<task_entry>* entries, str id, u64 ttl_ms, u64 poll_ms)
    throws std.json::error, std.alloc::alloc_error {
    purge(entries);
    switch (task_index(entries, id)) {
    case variant o::some(index): return o::some(detail_of(&(*entries)[*index], ttl_ms, poll_ms));
    case variant o::none: return o::none;
    }
}

protected o<std.json::value> board_detail(const task_board* board, str id) throws std.json::error, std.alloc::alloc_error {
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        return detail_in(std.sync::mutex_guard_mut(&guard), id, board->ttl_ms, board->poll_ms);
    case variant std.sync::lock_result::poisoned(move guard):
        return detail_in(std.sync::mutex_guard_mut(&guard), id, board->ttl_ms, board->poll_ms);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return o::none;
}

/* The signals of the runner of a task: the answers came, the task is to stop. */
protected struct task_signals { std.async::notify wake; std.async::notify stop; };

protected o<task_signals> signals_in(const array<task_entry>* entries, str id) {
    switch (task_index(entries, id)) {
    case variant o::some(index):
        const task_entry* entry = &(*entries)[*index];
        return o::some(task_signals {.wake = entry->wake.clone(), .stop = entry->stop.clone()});
    case variant o::none: return o::none;
    }
}

protected o<task_signals> board_signals(const task_board* board, str id) {
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): return signals_in(std.sync::mutex_guard_ref(&guard), id);
    case variant std.sync::lock_result::poisoned(move guard): return signals_in(std.sync::mutex_guard_ref(&guard), id);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return o::none;
}

/* Ends a task with a final status and its result or error; a task that already ended keeps
   its end. */
protected bool finish_in(array<task_entry>* entries, str id, task_status status, std.json::value outcome)
    throws std.alloc::alloc_error {
    switch (task_index(entries, id)) {
    case variant o::some(index):
        task_entry* entry = &(*entries)[*index];
        if (is_final(entry->status) == true) { return false; }
        entry->status = status;
        std.json::value old_outcome = core::replace(&entry->outcome, move outcome);
        drop old_outcome;
        std.json::value old_requests = core::replace(&entry->requests, std.json::null());
        drop old_requests;
        std.array::clear(&entry->outstanding);
        std.string::string now = time_text();
        std.string::string old = core::replace(&entry->updated, move now);
        drop old;
        return true;
    case variant o::none: break;
    }
    drop outcome;
    return false;
}

protected void board_finish(const task_board* board, str id, task_status status, std.json::value outcome)
    throws std.alloc::alloc_error {
    bool changed = false;
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        changed = finish_in(std.sync::mutex_guard_mut(&guard), id, status, move outcome);
    case variant std.sync::lock_result::poisoned(move guard):
        changed = finish_in(std.sync::mutex_guard_mut(&guard), id, status, move outcome);
    case variant std.sync::lock_result::would_deadlock: drop outcome;
    }
    if (changed == true) { announce_task(board, id); }
}

/* The key of an input request of a task: its round, a full stop and the key of the handler, so
   that no key returns over the life of the task. */
protected std.string::string round_key(u32 round, str key) throws std.alloc::alloc_error {
    std.string::string text = f"{round}.";
    std.string::append_str(&text, key);
    return move text;
}

/* Starts a round of input: the task waits for the answers to the requests of the handler. */
protected bool ask_in(array<task_entry>* entries, str id, const input_required* asked)
    throws std.json::error, std.alloc::alloc_error {
    switch (task_index(entries, id)) {
    case variant o::some(index):
        task_entry* entry = &(*entries)[*index];
        if (is_final(entry->status) == true) { return false; }
        entry->round += 1u32;
        std.json::value requests = std.json::object();
        std.array::clear(&entry->outstanding);
        std.array::clear(&entry->keys);
        std.array::clear(&entry->answers);
        for (usize at = 0usize; at < len(asked->requests); at += 1usize) {
            std.string::string key = round_key(entry->round, asked->keys[at]);
            put(&requests, key, elicitation_value(&asked->requests[at]));
            add_text(&entry->outstanding, key);
        }
        std.json::value old_requests = core::replace(&entry->requests, move requests);
        drop old_requests;
        o<std.string::string> state = o::none;
        switch (asked->state) {
        case variant o::some(text): state = o::some(std.string::from_str(*text));
        case variant o::none: break;
        }
        o<std.string::string> old_state = core::replace(&entry->state, move state);
        drop old_state;
        entry->status = task_status::input_required;
        std.string::string now = time_text();
        std.string::string old = core::replace(&entry->updated, move now);
        drop old;
        return true;
    case variant o::none: return false;
    }
}

protected bool board_ask(const task_board* board, str id, const input_required* asked)
    throws std.json::error, std.alloc::alloc_error {
    bool asking = false;
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): asking = ask_in(std.sync::mutex_guard_mut(&guard), id, asked);
    case variant std.sync::lock_result::poisoned(move guard): asking = ask_in(std.sync::mutex_guard_mut(&guard), id, asked);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    if (asking == true) { announce_task(board, id); }
    return asking;
}

/* The key of the handler under a key of a task: the part after its round. */
protected str handler_key(str key) {
    const u8[] bytes = key;
    for (usize at = 0usize; at < len(bytes); at += 1usize) {
        if (bytes[at] == 46u8) { return piece(key, at + 1usize, len(bytes)); }
    }
    return key;
}

/* The place of a key among the outstanding requests of a task; their count when it is not one. */
protected usize outstanding_slot(const array<std.string::string>* outstanding, str key) {
    for (usize slot = 0usize; slot < len(*outstanding); slot += 1usize) {
        if (std.bytes::equal((*outstanding)[slot], key) == true) { return slot; }
    }
    return len(*outstanding);
}

/* The answers of tasks/update: those to outstanding requests are kept, others ignored; when no
   request remains outstanding the task works again and its runner wakes. 0 when there is no such
   task, 2 when an answer is malformed, 1 otherwise. */
protected u8 answer_in(array<task_entry>* entries, str id, const std.json::value* responses)
    throws std.alloc::alloc_error {
    purge(entries);
    switch (task_index(entries, id)) {
    case variant o::some(index):
        task_entry* entry = &(*entries)[*index];
        if (entry->status != task_status::input_required) { return 1u8; }
        for (usize at = 0usize; at < std.json::len(responses); at += 1usize) {
            str key = std.json::key_at(responses, at);
            usize found = outstanding_slot(&entry->outstanding, key);
            if (found == len(entry->outstanding)) { continue; }
            o<answer> given = o::none;
            try {
                switch (std.json::find(responses, key)) {
                case variant o::some(response):
                    o<answer> read = answer_of(*response);
                    o<answer> old = core::replace(&given, move read);
                    drop old;
                case variant o::none: break;
                }
            } catch (std.json::error rejected) {
                (move rejected) as void;
                return 2u8;
            }
            switch (move given) {
            case variant o::some(move value):
                add_text(&entry->keys, handler_key(key));
                append(&entry->answers, move value);
                o<std.string::string> removed = entry->outstanding.remove(found);
                drop removed;
            case variant o::none: break;
            }
        }
        if (len(entry->outstanding) == 0usize) {
            entry->status = task_status::working;
            std.string::string now = time_text();
            std.string::string old = core::replace(&entry->updated, move now);
            drop old;
            entry->wake.notify_one();
        }
        return 1u8;
    case variant o::none: return 0u8;
    }
}

protected u8 board_answer(const task_board* board, str id, const std.json::value* responses)
    throws std.alloc::alloc_error {
    u8 taken = 0u8;
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): taken = answer_in(std.sync::mutex_guard_mut(&guard), id, responses);
    case variant std.sync::lock_result::poisoned(move guard): taken = answer_in(std.sync::mutex_guard_mut(&guard), id, responses);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    if (taken == 1u8) { announce_task(board, id); }
    return taken;
}

/* The input of the next run of the handler of a task whose answers came: the answers of the
   round under the keys of the handler and the state of the round. */
protected o<input> take_in(array<task_entry>* entries, str id, bool form, bool url) throws std.alloc::alloc_error {
    switch (task_index(entries, id)) {
    case variant o::some(index):
        task_entry* entry = &(*entries)[*index];
        if (is_final(entry->status) == true) { return o::none; }
        array<std.string::string> keys = core::replace(&entry->keys, std.array::create::<std.string::string>());
        array<answer> answers = core::replace(&entry->answers, std.array::create::<answer>());
        o<std.string::string> state = core::replace(&entry->state, o::none);
        std.json::value old_requests = core::replace(&entry->requests, std.json::null());
        drop old_requests;
        return o::some(input {.keys = move keys, .answers = move answers, .state = move state, .form = form,
                              .url = url});
    case variant o::none: return o::none;
    }
}

protected o<input> board_take(const task_board* board, str id, bool form, bool url) throws std.alloc::alloc_error {
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): return take_in(std.sync::mutex_guard_mut(&guard), id, form, url);
    case variant std.sync::lock_result::poisoned(move guard): return take_in(std.sync::mutex_guard_mut(&guard), id, form, url);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return o::none;
}

/* tasks/cancel: the runner of a task that has not ended is told to stop; a task that waits for
   input, whose handler does not run, is cancelled at once. 0 when there is no such task, 2 when
   it was cancelled at once, 1 otherwise. */
protected u8 cancel_in(array<task_entry>* entries, str id) throws std.alloc::alloc_error {
    purge(entries);
    switch (task_index(entries, id)) {
    case variant o::some(index):
        task_entry* entry = &(*entries)[*index];
        if (is_final(entry->status) == true) { return 1u8; }
        entry->stop.notify_one();
        if (entry->status != task_status::input_required) { return 1u8; }
        entry->status = task_status::cancelled;
        std.json::value old_requests = core::replace(&entry->requests, std.json::null());
        drop old_requests;
        std.array::clear(&entry->outstanding);
        std.string::string now = time_text();
        std.string::string old = core::replace(&entry->updated, move now);
        drop old;
        return 2u8;
    case variant o::none: return 0u8;
    }
}

protected bool board_cancel(const task_board* board, str id) throws std.alloc::alloc_error {
    u8 taken = 0u8;
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): taken = cancel_in(std.sync::mutex_guard_mut(&guard), id);
    case variant std.sync::lock_result::poisoned(move guard): taken = cancel_in(std.sync::mutex_guard_mut(&guard), id);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    if (taken == 2u8) { announce_task(board, id); }
    return taken != 0u8;
}

/* The ids among those asked for that name tasks of the board. */
protected array<std.string::string> known_in(array<task_entry>* entries, const array<std.string::string>* ids)
    throws std.alloc::alloc_error {
    purge(entries);
    array<std.string::string> found = std.array::create::<std.string::string>();
    for (usize at = 0usize; at < len(*ids); at += 1usize) {
        switch (task_index(entries, (*ids)[at])) {
        case variant o::some(index):
            index as void;
            add_text(&found, (*ids)[at]);
        case variant o::none: break;
        }
    }
    return move found;
}

protected array<std.string::string> board_known(const task_board* board, const array<std.string::string>* ids)
    throws std.alloc::alloc_error {
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): return known_in(std.sync::mutex_guard_mut(&guard), ids);
    case variant std.sync::lock_result::poisoned(move guard): return known_in(std.sync::mutex_guard_mut(&guard), ids);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return std.array::create::<std.string::string>();
}

/* ---- Requests of the Tasks extension ---- */

/* The -32021 of a client that does not declare the Tasks extension. */
protected refusal missing_tasks() throws std.json::error, std.alloc::alloc_error {
    std.json::value extensions = std.json::object();
    put(&extensions, tasks_extension, std.json::object());
    std.json::value required = std.json::object();
    put(&required, "extensions", move extensions);
    std.json::value data = std.json::object();
    put(&data, "requiredCapabilities", move required);
    return refusal {.code = missing_required_client_capability,
                    .message = std.string::from_str("Missing required client capability"),
                    .data = o::some(move data), .status = 400u16};
}

/* The empty result of tasks/update and tasks/cancel. */
protected outgoing acknowledged(const implementation* info, const admitted* request)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    finish(&result, info, "complete");
    return success(&request->id, move result);
}

protected outgoing task_not_found(const admitted* request) throws std.alloc::alloc_error {
    return invalid_params(&request->id, "Failed to retrieve task: Task not found");
}

/* R-SLIB-MCP-0022: tasks/get, tasks/update and tasks/cancel. */
@generic<S: send & sync & unborrowed>
protected outgoing answer_task_request(const server<S>* host, const admitted* request)
    throws std.json::error, std.alloc::alloc_error {
    if (request->tasks == false) { return refuse(&request->id, missing_tasks()); }
    o<arc task_board> found = shared_board(host);
    switch (move found) {
    case variant o::some(move board):
        str id = "";
        switch (member_text(&request->params, "taskId")) {
        case variant o::some(text): id = *text;
        case variant o::none: return invalid_params(&request->id, "Invalid params: taskId");
        }
        if (is_method(request, "tasks/get") == true) {
            o<std.json::value> detail = board_detail(&*board, id);
            switch (move detail) {
            case variant o::some(move value):
                std.json::value result = move value;
                finish(&result, &host->info, "complete");
                return success(&request->id, move result);
            case variant o::none: return task_not_found(request);
            }
        }
        if (is_method(request, "tasks/update") == true) {
            switch (member(&request->params, "inputResponses")) {
            case variant o::some(responses):
                if (std.json::kind(*responses) != std.json::value_kind::object) {
                    return invalid_params(&request->id, "Invalid params: inputResponses");
                }
                u8 taken = board_answer(&*board, id, *responses);
                if (taken == 0u8) { return task_not_found(request); }
                if (taken == 2u8) { return invalid_params(&request->id, "Invalid params: inputResponses"); }
                return acknowledged(&host->info, request);
            case variant o::none: return invalid_params(&request->id, "Invalid params: inputResponses");
            }
        }
        if (board_cancel(&*board, id) == false) { return task_not_found(request); }
        return acknowledged(&host->info, request);
    case variant o::none: break;
    }
    return refuse(&request->id, refusal_of(std.jsonrpc::method_not_found, "Method not found", 404u16));
}

/* R-SLIB-MCP-0022: a call of a tool that runs as a task: the task is made and handed to the
   runner, and the call is answered with the first state of the task and resultType "task". */
@generic<S: send & sync & unborrowed>
protected outgoing start_task(const server<S>* host, const admitted* request, usize tool, call context)
    throws std.json::error, std.alloc::alloc_error {
    o<arc task_board> found = shared_board(host);
    switch (move found) {
    case variant o::some(move board):
        std.uuid::uuid made = std.uuid::v4();
        std.string::string id = f"{made}";
        o<std.json::value> seed = board_create(&*board, id);
        switch (move seed) {
        case variant o::some(move value):
            std.string::string name = core::replace(&context.name, std.string::create());
            std.json::value arguments = core::replace(&context.arguments, std.json::null());
            drop context;
            task_job job = {.id = std.string::from_str(id), .tool = tool, .name = move name,
                            .arguments = move arguments, .form = request->form, .url = request->url};
            std.sync::send_result<task_work> sent = std.sync::send(&board->jobs, task_work::start(move job));
            drop sent;
            std.json::value result = move value;
            finish(&result, &host->info, "task");
            return success(&request->id, move result);
        case variant o::none: drop context;
        }
    case variant o::none: drop context;
    }
    return internal_error(&request->id);
}

/* ---- tools/call ---- */

protected enum tool_plan { ready(call), refused(refusal) };

@generic<S: send & sync & unborrowed>
protected tool_plan plan_call(const server<S>* host, admitted* request, const (std.sync::sync_sender<outgoing>)* sender,
                              usize* chosen)
    throws std.json::error, std.alloc::alloc_error {
    str wanted = "";
    switch (member_text(&request->params, "name")) {
    case variant o::some(name): wanted = *name;
    case variant o::none:
        return tool_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: name", 200u16));
    }
    bool found = false;
    for (usize index = 0usize; index < len(host->tools); index += 1usize) {
        if (std.bytes::equal(host->tools[index].definition.name, wanted) == true) {
            *chosen = index;
            found = true;
            break;
        }
    }
    if (found == false) {
        std.string::string text = f"Unknown tool: {wanted}";
        return tool_plan::refused(refusal_of(std.jsonrpc::invalid_params, text, 200u16));
    }
    o<std.json::value> given_arguments = o::none;
    switch (member(&request->params, "arguments")) {
    case variant o::some(value):
        if (std.json::kind(*value) != std.json::value_kind::object) {
            return tool_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: arguments", 200u16));
        }
        given_arguments = o::some(copy_value(*value));
    case variant o::none: break;
    }
    std.json::value arguments = object_or_empty(move given_arguments);
    input given = input_of(&request->params, request->form, request->url);
    std.string::string tool_name = std.string::from_str(wanted);
    o<std.json::value> token = core::replace(&request->token, o::none);
    return tool_plan::ready(call {.name = move tool_name, .arguments = move arguments, .input = move given,
                                  .progress = progress_of(move token, sender)});
}

@generic<S: send & sync & unborrowed>
protected outgoing tool_reply(const server<S>* host, const admitted* request, tool_outcome outcome)
    throws std.json::error, std.alloc::alloc_error {
    switch (move outcome) {
    case variant tool_outcome::complete(move result):
        std.json::value body = to_value(&result);
        finish(&body, &host->info, "complete");
        return success(&request->id, move body);
    case variant tool_outcome::needs_input(move asked): return input_reply(&host->info, request, &asked);
    }
}

@generic<S: send & sync & unborrowed>
protected async outgoing call_tool(arc server<S> host, admitted request, std.sync::sync_sender<outgoing> out)
    throws std.error::fault {
    usize index = 0usize;
    o<tool_plan> planned = o::none;
    try {
        tool_plan made = plan_call(&*host, &request, &out, &index);
        o<tool_plan> old = core::replace(&planned, o::some(move made));
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
        return invalid_params(&request.id, "Invalid params: inputResponses");
    }
    drop out;
    switch (move planned) {
    case variant o::some(move plan):
        switch (move plan) {
        case variant tool_plan::refused(move problem): return refuse(&request.id, move problem);
        case variant tool_plan::ready(move context):
            u8 mode = host->tools[index].task_mode;
            if (mode != 0u8 && request.tasks == true && runner_attached(&*host) == true) {
                try {
                    return start_task(&*host, &request, index, move context);
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                }
                return internal_error(&request.id);
            }
            if (mode == 2u8 && request.tasks == false) {
                drop context;
                try {
                    return refuse(&request.id, missing_tasks());
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                }
                return internal_error(&request.id);
            }
            auto handler = host->tools[index].handler;
            try {
                tool_outcome outcome = await handler(std.arc::clone(&host->state), move context);
                return tool_reply(&*host, &request, move outcome);
            } catch (std.error::fault rejected) {
                rejected as void;
            } catch (std.json::error rejected) {
                (move rejected) as void;
            }
        }
    case variant o::none: break;
    }
    return internal_error(&request.id);
}

/* ---- resources/read ---- */

/* Whether the URI matches the template; the values of its variables, percent-decoded, are added
   to names and values. A {name} value is nonempty and has no '/'; a {+name} value is nonempty. */
protected bool template_match(str pattern, str uri, array<std.string::string>* names,
                              array<std.string::string>* values) throws std.alloc::alloc_error {
    const u8[] shape = pattern;
    const u8[] given = uri;
    usize at = 0usize;
    usize position = 0usize;
    while (at < len(shape)) {
        if (shape[at] != 123u8) {
            if (position >= len(given) || given[position] != shape[at]) { return false; }
            at += 1usize;
            position += 1usize;
            continue;
        }
        usize end = 0usize;
        switch (variable_end(shape, at)) {
        case variant o::some(found): end = *found;
        case variant o::none: return false;
        }
        usize first = at + 1usize;
        bool reserved = shape[first] == 43u8;
        if (reserved == true) { first += 1usize; }
        usize literal = end + 1usize;
        usize literal_end = literal;
        while (literal_end < len(shape) && shape[literal_end] != 123u8) { literal_end += 1usize; }
        usize size = literal_end - literal;
        usize value_end = position;
        bool found_end = false;
        while (value_end <= len(given)) {
            if (size == 0usize) {
                if (value_end == len(given)) {
                    found_end = true;
                    break;
                }
            } else {
                if (value_end + size <= len(given) &&
                    std.bytes::equal(given[value_end..value_end + size], shape[literal..literal_end]) == true) {
                    found_end = true;
                    break;
                }
            }
            if (value_end == len(given)) { break; }
            if (reserved == false && given[value_end] == 47u8) { break; }
            value_end += 1usize;
        }
        if (found_end == false || value_end == position) { return false; }
        try {
            std.string::string value = std.url::percent_decode(piece(uri, position, value_end));
            add_text(names, piece(pattern, first, end));
            append(values, move value);
        } catch (std.url::url_error rejected) {
            rejected as void;
            return false;
        }
        position = value_end;
        at = end + 1usize;
    }
    return position == len(given);
}

/* What serves a read: an added resource or a template, and the values of its variables. */
protected struct read_target { bool from_template; usize index; };

protected enum read_plan { ready(read), refused(refusal) };

protected refusal not_found(str uri) throws std.json::error, std.alloc::alloc_error {
    std.json::value data = std.json::object();
    put_text(&data, "uri", uri);
    return refusal {.code = std.jsonrpc::invalid_params, .message = std.string::from_str("Resource not found"),
                    .data = o::some(move data), .status = 200u16};
}

@generic<S: send & sync & unborrowed>
protected read_plan plan_read(const server<S>* host, admitted* request, const (std.sync::sync_sender<outgoing>)* sender,
                              read_target* chosen)
    throws std.json::error, std.alloc::alloc_error {
    str uri = "";
    switch (member_text(&request->params, "uri")) {
    case variant o::some(text): uri = *text;
    case variant o::none:
        return read_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: uri", 200u16));
    }
    array<std.string::string> names = std.array::create::<std.string::string>();
    array<std.string::string> values = std.array::create::<std.string::string>();
    bool found = false;
    for (usize index = 0usize; index < len(host->resources); index += 1usize) {
        if (std.bytes::equal(host->resources[index].definition.uri, uri) == true) {
            *chosen = read_target {.from_template = false, .index = index};
            found = true;
            break;
        }
    }
    if (found == false) {
        for (usize index = 0usize; index < len(host->templates); index += 1usize) {
            std.array::clear(&names);
            std.array::clear(&values);
            if (template_match(host->templates[index].definition.uri_template, uri, &names, &values) == true) {
                *chosen = read_target {.from_template = true, .index = index};
                found = true;
                break;
            }
        }
    }
    if (found == false) {
        drop names;
        drop values;
        return read_plan::refused(not_found(uri));
    }
    input given = input_of(&request->params, request->form, request->url);
    std.string::string address = std.string::from_str(uri);
    o<std.json::value> token = core::replace(&request->token, o::none);
    return read_plan::ready(read {.uri = move address, .names = move names, .values = move values,
                                  .input = move given, .progress = progress_of(move token, sender)});
}

@generic<S: send & sync & unborrowed>
protected outgoing read_reply(const server<S>* host, const admitted* request, str uri, read_outcome outcome)
    throws std.json::error, std.alloc::alloc_error {
    switch (move outcome) {
    case variant read_outcome::complete(move items):
        std.json::value entries = std.json::array();
        for (usize index = 0usize; index < len(items); index += 1usize) {
            std.json::append(&entries, contents_value(&items[index]));
        }
        std.json::value result = std.json::object();
        put(&result, "contents", move entries);
        cache_hints(&result, &host->settings);
        finish(&result, &host->info, "complete");
        return success(&request->id, move result);
    case variant read_outcome::needs_input(move asked): return input_reply(&host->info, request, &asked);
    case variant read_outcome::not_found: return refuse(&request->id, not_found(uri));
    }
}

/* The handler of the resource or template that serves a read. */
@generic<S: send & sync & unborrowed>
protected (async fn(arc S, read) -> read_outcome throws(std.error::fault))
reader_of(const server<S>* host, read_target target) {
    if (target.from_template == true) { return host->templates[target.index].handler; }
    return host->resources[target.index].handler;
}

@generic<S: send & sync & unborrowed>
protected async outgoing read_resource(arc server<S> host, admitted request, std.sync::sync_sender<outgoing> out)
    throws std.error::fault {
    read_target target = {.from_template = false, .index = 0usize};
    o<read_plan> planned = o::none;
    try {
        read_plan made = plan_read(&*host, &request, &out, &target);
        o<read_plan> old = core::replace(&planned, o::some(move made));
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
        return invalid_params(&request.id, "Invalid params: inputResponses");
    }
    drop out;
    switch (move planned) {
    case variant o::some(move plan):
        switch (move plan) {
        case variant read_plan::refused(move problem): return refuse(&request.id, move problem);
        case variant read_plan::ready(move context):
            std.string::string uri = std.string::from_str(context.uri);
            auto handler = reader_of(&*host, target);
            try {
                read_outcome outcome = await handler(std.arc::clone(&host->state), move context);
                return read_reply(&*host, &request, uri, move outcome);
            } catch (std.error::fault rejected) {
                rejected as void;
            } catch (std.json::error rejected) {
                (move rejected) as void;
            }
        }
    case variant o::none: break;
    }
    return internal_error(&request.id);
}

/* ---- prompts/get ---- */

/* Adds the members of the object under key, whose values shall be strings, to names and
   values; false when the member is no such object. */
protected bool string_pairs(const std.json::value* params, str key, array<std.string::string>* names,
                            array<std.string::string>* values) throws std.alloc::alloc_error {
    switch (member(params, key)) {
    case variant o::some(given):
        if (std.json::kind(*given) != std.json::value_kind::object) { return false; }
        for (usize index = 0usize; index < std.json::len(*given); index += 1usize) {
            str name = std.json::key_at(*given, index);
            switch (member_text(*given, name)) {
            case variant o::some(value):
                add_text(names, name);
                add_text(values, *value);
            case variant o::none: return false;
            }
        }
    case variant o::none: break;
    }
    return true;
}

protected enum prompt_plan { ready(prompt_call), refused(refusal) };

@generic<S: send & sync & unborrowed>
protected prompt_plan plan_prompt(const server<S>* host, admitted* request,
                                  const (std.sync::sync_sender<outgoing>)* sender, usize* chosen)
    throws std.json::error, std.alloc::alloc_error {
    str wanted = "";
    switch (member_text(&request->params, "name")) {
    case variant o::some(name): wanted = *name;
    case variant o::none:
        return prompt_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: name", 200u16));
    }
    bool found = false;
    for (usize index = 0usize; index < len(host->prompts); index += 1usize) {
        if (std.bytes::equal(host->prompts[index].definition.name, wanted) == true) {
            *chosen = index;
            found = true;
            break;
        }
    }
    if (found == false) {
        std.string::string text = f"Unknown prompt: {wanted}";
        return prompt_plan::refused(refusal_of(std.jsonrpc::invalid_params, text, 200u16));
    }
    array<std.string::string> names = std.array::create::<std.string::string>();
    array<std.string::string> values = std.array::create::<std.string::string>();
    if (string_pairs(&request->params, "arguments", &names, &values) == false) {
        drop names;
        drop values;
        return prompt_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: arguments", 200u16));
    }
    const prompt* definition = &host->prompts[*chosen].definition;
    for (usize index = 0usize; index < len(definition->arguments); index += 1usize) {
        if (definition->arguments[index].required == false) { continue; }
        str name = definition->arguments[index].name;
        if (present(lookup(&names, &values, name)) == false) {
            std.string::string text = f"Missing required argument: {name}";
            drop names;
            drop values;
            return prompt_plan::refused(refusal_of(std.jsonrpc::invalid_params, text, 200u16));
        }
    }
    input given_input = input_of(&request->params, request->form, request->url);
    std.string::string prompt_name = std.string::from_str(wanted);
    o<std.json::value> token = core::replace(&request->token, o::none);
    return prompt_plan::ready(prompt_call {.name = move prompt_name, .names = move names, .values = move values,
                                           .input = move given_input, .progress = progress_of(move token, sender)});
}

@generic<S: send & sync & unborrowed>
protected outgoing prompt_reply(const server<S>* host, const admitted* request, prompt_outcome outcome)
    throws std.json::error, std.alloc::alloc_error {
    switch (move outcome) {
    case variant prompt_outcome::complete(move result):
        std.json::value body = to_value(&result);
        finish(&body, &host->info, "complete");
        return success(&request->id, move body);
    case variant prompt_outcome::needs_input(move asked): return input_reply(&host->info, request, &asked);
    }
}

@generic<S: send & sync & unborrowed>
protected async outgoing get_prompt(arc server<S> host, admitted request, std.sync::sync_sender<outgoing> out)
    throws std.error::fault {
    usize index = 0usize;
    o<prompt_plan> planned = o::none;
    try {
        prompt_plan made = plan_prompt(&*host, &request, &out, &index);
        o<prompt_plan> old = core::replace(&planned, o::some(move made));
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
        return invalid_params(&request.id, "Invalid params: inputResponses");
    }
    drop out;
    switch (move planned) {
    case variant o::some(move plan):
        switch (move plan) {
        case variant prompt_plan::refused(move problem): return refuse(&request.id, move problem);
        case variant prompt_plan::ready(move context):
            auto handler = host->prompts[index].handler;
            try {
                prompt_outcome outcome = await handler(std.arc::clone(&host->state), move context);
                return prompt_reply(&*host, &request, move outcome);
            } catch (std.error::fault rejected) {
                rejected as void;
            } catch (std.json::error rejected) {
                (move rejected) as void;
            }
        }
    case variant o::none: break;
    }
    return internal_error(&request.id);
}

/* ---- completion/complete ---- */

/* Whether a URI template has a variable of the name. */
protected bool template_has(str pattern, str name) {
    const u8[] shape = pattern;
    usize at = 0usize;
    while (at < len(shape)) {
        if (shape[at] != 123u8) {
            at += 1usize;
            continue;
        }
        switch (variable_end(shape, at)) {
        case variant o::some(end):
            usize first = at + 1usize;
            if (first < *end && shape[first] == 43u8) { first += 1usize; }
            if (std.bytes::equal(shape[first..*end], name) == true) { return true; }
            at = *end + 1usize;
        case variant o::none: return false;
        }
    }
    return false;
}

protected enum completion_plan { ready(completion_request), refused(refusal) };

@generic<S: send & sync & unborrowed>
protected bool prompt_has(const server<S>* host, str prompt_name, str argument, bool* known) {
    for (usize index = 0usize; index < len(host->prompts); index += 1usize) {
        const prompt* definition = &host->prompts[index].definition;
        if (std.bytes::equal(definition->name, prompt_name) == true) {
            *known = true;
            for (usize at = 0usize; at < len(definition->arguments); at += 1usize) {
                if (std.bytes::equal(definition->arguments[at].name, argument) == true) { return true; }
            }
            return false;
        }
    }
    return false;
}

@generic<S: send & sync & unborrowed>
protected bool template_offers(const server<S>* host, str pattern, str argument, bool* known) {
    for (usize index = 0usize; index < len(host->templates); index += 1usize) {
        str shape = host->templates[index].definition.uri_template;
        if (std.bytes::equal(shape, pattern) == true) {
            *known = true;
            return template_has(shape, argument);
        }
    }
    return false;
}

@generic<S: send & sync & unborrowed>
protected completion_plan plan_completion(const server<S>* host, const std.json::value* params)
    throws std.json::error, std.alloc::alloc_error {
    bool is_prompt = false;
    str reference = "";
    switch (member(params, "ref")) {
    case variant o::some(target):
        switch (member_text(*target, "type")) {
        case variant o::some(kind):
            if (std.bytes::equal(*kind, "ref/prompt") == true) {
                is_prompt = true;
                switch (member_text(*target, "name")) {
                case variant o::some(name): reference = *name;
                case variant o::none:
                    return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: ref", 200u16));
                }
            } else {
                if (std.bytes::equal(*kind, "ref/resource") == false) {
                    return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: ref", 200u16));
                }
                switch (member_text(*target, "uri")) {
                case variant o::some(uri): reference = *uri;
                case variant o::none:
                    return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: ref", 200u16));
                }
            }
        case variant o::none:
            return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: ref", 200u16));
        }
    case variant o::none:
        return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: ref", 200u16));
    }
    str argument = "";
    str typed = "";
    switch (member(params, "argument")) {
    case variant o::some(given):
        switch (member_text(*given, "name")) {
        case variant o::some(name): argument = *name;
        case variant o::none:
            return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: argument", 200u16));
        }
        switch (member_text(*given, "value")) {
        case variant o::some(value): typed = *value;
        case variant o::none:
            return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: argument", 200u16));
        }
    case variant o::none:
        return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: argument", 200u16));
    }
    bool known = false;
    bool offered_argument = is_prompt == true ? prompt_has(host, reference, argument, &known)
                                              : template_offers(host, reference, argument, &known);
    if (offered_argument == false) {
        if (known == false) {
            std.string::string text = f"Unknown reference: {reference}";
            return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, text, 200u16));
        }
        std.string::string text = f"Unknown argument: {argument}";
        return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, text, 200u16));
    }
    array<std.string::string> names = std.array::create::<std.string::string>();
    array<std.string::string> values = std.array::create::<std.string::string>();
    bool valid_context = true;
    switch (member(params, "context")) {
    case variant o::some(context): valid_context = string_pairs(*context, "arguments", &names, &values);
    case variant o::none: break;
    }
    if (valid_context == false) {
        drop names;
        drop values;
        return completion_plan::refused(refusal_of(std.jsonrpc::invalid_params, "Invalid params: context", 200u16));
    }
    return completion_plan::ready(completion_request {
        .prompt = is_prompt, .reference = std.string::from_str(reference), .argument = std.string::from_str(argument),
        .value = std.string::from_str(typed), .names = move names, .values = move values});
}

@generic<S: send & sync & unborrowed>
protected outgoing completion_reply(const server<S>* host, const admitted* request, const completion* found)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value values = std.json::array();
    usize count = len(found->values);
    bool more = found->has_more;
    if (count > 100usize) {
        count = 100usize;
        more = true;
    }
    for (usize index = 0usize; index < count; index += 1usize) {
        std.json::append(&values, std.json::from_string(found->values[index]));
    }
    std.json::value body = std.json::object();
    put(&body, "values", move values);
    switch (found->total) {
    case variant o::some(total): put(&body, "total", json_unsigned(*total));
    case variant o::none: break;
    }
    if (more == true) { put_flag(&body, "hasMore"); }
    std.json::value result = std.json::object();
    put(&result, "completion", move body);
    finish(&result, &host->info, "complete");
    return success(&request->id, move result);
}

@generic<S: send & sync & unborrowed>
protected async outgoing complete(arc server<S> host, admitted request) throws std.error::fault {
    o<completion_plan> planned = o::none;
    try {
        completion_plan made = plan_completion(&*host, &request.params);
        o<completion_plan> old = core::replace(&planned, o::some(move made));
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    switch (move planned) {
    case variant o::some(move plan):
        switch (move plan) {
        case variant completion_plan::refused(move problem): return refuse(&request.id, move problem);
        case variant completion_plan::ready(move asked):
            auto chosen = host->completer;
            switch (chosen) {
            case variant o::some(completer):
                auto run_completer = *completer;
                try {
                    completion found = await run_completer(std.arc::clone(&host->state), move asked);
                    return completion_reply(&*host, &request, &found);
                } catch (std.error::fault rejected) {
                    rejected as void;
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                }
            case variant o::none: drop asked;
            }
        }
    case variant o::none: break;
    }
    return internal_error(&request.id);
}

/* ---- subscriptions/listen ---- */

/* The notifications that a subscription receives: (R-SLIB-MCP-0023) tasks are the ids of the
   tasks whose states it receives. */
protected struct filter {
    bool tools;
    bool prompts;
    bool resources;
    array<std.string::string> uris;
    array<std.string::string> tasks;
};

/* The texts of a JSON array of strings; other items are skipped. */
protected array<std.string::string> texts_of(const std.json::value* items) throws std.alloc::alloc_error {
    array<std.string::string> found = std.array::create::<std.string::string>();
    for (usize index = 0usize; index < std.json::len(items); index += 1usize) {
        switch (std.json::get(items, index)) {
        case variant o::some(item):
            if (std.json::kind(*item) == std.json::value_kind::string) { add_text(&found, std.json::text(*item)); }
        case variant o::none: break;
        }
    }
    return move found;
}

/* R-SLIB-MCP-0023: the tasks a subscription asks for that the board knows. */
@generic<S: send & sync & unborrowed>
protected array<std.string::string> granted_tasks(const server<S>* host, const std.json::value* wanted)
    throws std.alloc::alloc_error {
    switch (member(wanted, "taskIds")) {
    case variant o::some(ids):
        array<std.string::string> asked = texts_of(*ids);
        switch (host->board) {
        case variant o::some(board): return board_known(&**board, &asked);
        case variant o::none: drop asked;
        }
    case variant o::none: break;
    }
    return std.array::create::<std.string::string>();
}

/* Whether a subscription asks for the states of tasks. */
protected bool asks_tasks(const std.json::value* params) {
    switch (member(params, "notifications")) {
    case variant o::some(wanted):
        switch (member(*wanted, "taskIds")) {
        case variant o::some(ids):
            ids as void;
            return true;
        case variant o::none: break;
        }
    case variant o::none: break;
    }
    return false;
}

/* The part of the notifications a subscription asks for that the server honours; none when the
   request has no notifications object. */
@generic<S: send & sync & unborrowed>
protected o<filter> filter_of(const server<S>* host, const std.json::value* params) throws std.alloc::alloc_error {
    switch (member(params, "notifications")) {
    case variant o::some(wanted):
        if (std.json::kind(*wanted) != std.json::value_kind::object) { return o::none; }
        bool changed = host->settings.list_changed;
        filter granted = {.tools = changed == true && len(host->tools) != 0usize &&
                                   member_true(*wanted, "toolsListChanged") == true,
                          .prompts = changed == true && len(host->prompts) != 0usize &&
                                     member_true(*wanted, "promptsListChanged") == true,
                          .resources = changed == true && has_resources(host) == true &&
                                       member_true(*wanted, "resourcesListChanged") == true,
                          .uris = std.array::create::<std.string::string>(), .tasks = granted_tasks(host, *wanted)};
        if (host->settings.subscribe == true && has_resources(host) == true) {
            switch (member(*wanted, "resourceSubscriptions")) {
            case variant o::some(uris):
                for (usize index = 0usize; index < std.json::len(*uris); index += 1usize) {
                    switch (std.json::get(*uris, index)) {
                    case variant o::some(item):
                        if (std.json::kind(*item) == std.json::value_kind::string) {
                            add_text(&granted.uris, std.json::text(*item));
                        }
                    case variant o::none: break;
                    }
                }
            case variant o::none: break;
            }
        }
        return o::some(move granted);
    case variant o::none: break;
    }
    return o::none;
}

protected std.json::value filter_value(const filter* granted) throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    if (granted->tools == true) { put_flag(&result, "toolsListChanged"); }
    if (granted->prompts == true) { put_flag(&result, "promptsListChanged"); }
    if (granted->resources == true) { put_flag(&result, "resourcesListChanged"); }
    if (len(granted->uris) != 0usize) {
        std.json::value uris = std.json::array();
        for (usize index = 0usize; index < len(granted->uris); index += 1usize) {
            std.json::append(&uris, std.json::from_string(granted->uris[index]));
        }
        put(&result, "resourceSubscriptions", move uris);
    }
    if (len(granted->tasks) != 0usize) {
        std.json::value ids = std.json::array();
        for (usize index = 0usize; index < len(granted->tasks); index += 1usize) {
            std.json::append(&ids, std.json::from_string(granted->tasks[index]));
        }
        put(&result, "taskIds", move ids);
    }
    return move result;
}

/* A notification of a subscription: its method, the URI of an updated resource and the id of
   the subscription in _meta. */
protected outgoing subscription_note(const std.jsonrpc::request_id* id, str method, o<str> uri,
                                     o<std.json::value> notifications)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value params = std.json::object();
    switch (uri) {
    case variant o::some(value): put_text(&params, "uri", *value);
    case variant o::none: break;
    }
    switch (move notifications) {
    case variant o::some(move value): put(&params, "notifications", move value);
    case variant o::none: break;
    }
    std.json::value meta = std.json::object();
    put(&meta, key_subscription, id->to_json());
    put(&params, "_meta", move meta);
    std.jsonrpc::notification note = {.method = std.string::from_str(method), .params = o::some(move params)};
    return outgoing {.message = o::some(std.jsonrpc::message::notification(move note)), .status = 200u16};
}

/* Whether a subscription covers a URI: the URI itself or a resource below it. */
protected bool covers(const filter* granted, str uri) {
    const u8[] given = uri;
    for (usize index = 0usize; index < len(granted->uris); index += 1usize) {
        const u8[] watched = granted->uris[index];
        if (std.bytes::equal(watched, given) == true) { return true; }
        if (len(watched) != 0usize && len(given) > len(watched) &&
            std.bytes::equal(given[0usize..len(watched)], watched) == true &&
            (watched[len(watched) - 1usize] == 47u8 || given[len(watched)] == 47u8)) {
            return true;
        }
    }
    return false;
}

/* Whether a subscription receives the states of a task. */
protected bool watches_task(const filter* granted, str identifier) {
    for (usize index = 0usize; index < len(granted->tasks); index += 1usize) {
        if (std.bytes::equal(granted->tasks[index], identifier) == true) { return true; }
    }
    return false;
}

/* R-SLIB-MCP-0023: notifications/tasks: the state of a task as tasks/get gives it, with the id of
   the subscription; none when the task is gone. */
protected o<outgoing> task_note(const std.jsonrpc::request_id* id, const (o<arc task_board>)* board, str identifier)
    throws std.json::error, std.alloc::alloc_error {
    switch (*board) {
    case variant o::some(shared):
        o<std.json::value> detail = board_detail(&**shared, identifier);
        switch (move detail) {
        case variant o::some(move value):
            std.json::value params = move value;
            std.json::value meta = std.json::object();
            put(&meta, key_subscription, id->to_json());
            put(&params, "_meta", move meta);
            std.jsonrpc::notification note = {.method = std.string::from_str("notifications/tasks"),
                                              .params = o::some(move params)};
            return o::some(outgoing {.message = o::some(std.jsonrpc::message::notification(move note)),
                                     .status = 200u16});
        case variant o::none: break;
        }
    case variant o::none: break;
    }
    return o::none;
}

/* The notifications of one change for a subscription: none when it does not ask for it. */
protected o<outgoing> change_note(const std.jsonrpc::request_id* id, const filter* granted,
                                  const (o<arc task_board>)* board, str text)
    throws std.json::error, std.alloc::alloc_error {
    const u8[] bytes = text;
    if (len(bytes) == 0usize) { return o::none; }
    u8 kind = bytes[0usize];
    if (kind == 116u8 && granted->tools == true) {
        return o::some(subscription_note(id, "notifications/tools/list_changed", o::none, o::none));
    }
    if (kind == 112u8 && granted->prompts == true) {
        return o::some(subscription_note(id, "notifications/prompts/list_changed", o::none, o::none));
    }
    if (kind == 114u8 && granted->resources == true) {
        return o::some(subscription_note(id, "notifications/resources/list_changed", o::none, o::none));
    }
    if (kind == 117u8) {
        str uri = piece(text, 1usize, len(bytes));
        if (covers(granted, uri) == true) {
            return o::some(subscription_note(id, "notifications/resources/updated", o::some(uri), o::none));
        }
    }
    if (kind == 107u8) {
        str identifier = piece(text, 1usize, len(bytes));
        if (watches_task(granted, identifier) == true) { return task_note(id, board, identifier); }
    }
    return o::none;
}

/* Sends a message, waiting while the stream of the request is behind; false when it is gone. */
@scoped
protected async bool deliver(const (std.sync::sync_sender<outgoing>)* sender, outgoing item) throws std.error::fault {
    std.sync::reserve_result<outgoing> room = await std.sync::reserve(sender);
    switch (move room) {
    case variant std.sync::reserve_result::reserved(move permit):
        std.sync::send_permit(move permit, move item);
        return true;
    case variant std.sync::reserve_result::disconnected:
        drop item;
        return false;
    }
    return false;
}

/* Sends what a subscription missed while it lagged: every list it watches changed, and so did
   every resource; each task it watches sends its state. */
@scoped
protected async bool send_missed(const (std.sync::sync_sender<outgoing>)* sender, const std.jsonrpc::request_id* id,
                                 const filter* granted, const (o<arc task_board>)* board) throws std.error::fault {
    array<outgoing> notes = std.array::create::<outgoing>();
    try {
        if (granted->tools == true) {
            append(&notes, subscription_note(id, "notifications/tools/list_changed", o::none, o::none));
        }
        if (granted->prompts == true) {
            append(&notes, subscription_note(id, "notifications/prompts/list_changed", o::none, o::none));
        }
        if (granted->resources == true) {
            append(&notes, subscription_note(id, "notifications/resources/list_changed", o::none, o::none));
        }
        for (usize index = 0usize; index < len(granted->uris); index += 1usize) {
            append(&notes, subscription_note(id, "notifications/resources/updated",
                                             o::some(granted->uris[index]), o::none));
        }
        for (usize index = 0usize; index < len(granted->tasks); index += 1usize) {
            o<outgoing> state = task_note(id, board, granted->tasks[index]);
            switch (move state) {
            case variant o::some(move item): append(&notes, move item);
            case variant o::none: break;
            }
        }
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    bool open = true;
    bool more = true;
    while (open == true && more == true) {
        o<outgoing> next = notes.remove(0usize);
        switch (move next) {
        case variant o::some(move item):
            task_scope(1) io { open = await deliver(sender, move item); }
        case variant o::none: more = false;
        }
    }
    drop notes;
    return open;
}

/* R-SLIB-MCP-0015: a subscription: the acknowledgement of the notifications that the server
   honours, then those notifications as the program announces changes, each with the id of the
   subscription; with keep-alive, a message of none every 15 seconds without one. It ends when
   its stream is gone or the notifier of the server is closed. */
@generic<S: send & sync & unborrowed>
protected async void watch(arc server<S> host, std.jsonrpc::request_id id, filter granted,
                           std.sync::sync_sender<outgoing> out, bool keepalive) throws std.error::fault {
    std.async::broadcast_receiver<std.string::string> events = std.async::subscribe(&host->changes.sender);
    o<arc task_board> board = shared_board(&*host);
    drop host;
    o<outgoing> ack = o::none;
    try {
        outgoing made = subscription_note(&id, "notifications/subscriptions/acknowledged", o::none,
                                          o::some(filter_value(&granted)));
        o<outgoing> old = core::replace(&ack, o::some(move made));
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    bool open = false;
    switch (move ack) {
    case variant o::some(move item):
        task_scope(1) io { open = await deliver(&out, move item); }
    case variant o::none: break;
    }
    while (open == true) {
        o<std.async::broadcast_result<std.string::string>> next = o::none;
        task_scope(1) wait {
            // The receive stays pending across the keep-alives: cancelling one that has already
            // taken a change would lose it (P4.1-6).
            auto receiving = events.receive();
            bool waiting = keepalive;
            while (waiting == true) {
                std.time::instant now = std.time::monotonic_now();
                std.time::instant limit = std.time::instant_add(now, std.time::duration_from_seconds(15i64));
                o<usize> ready = await wait.first_until(limit, &receiving);
                switch (ready) {
                case variant o::some(index):
                    index as void;
                    waiting = false;
                case variant o::none:
                    task_scope(1) io { open = await deliver(&out, outgoing {.message = o::none, .status = 200u16}); }
                    if (open == false) { waiting = false; }
                }
            }
            if (open == true) {
                std.async::broadcast_result<std.string::string> got = await move receiving;
                next = o::some(move got);
            } else {
                std.async::cancel(move receiving);
            }
        }
        o<outgoing> note = o::none;
        bool lagged = false;
        switch (move next) {
        case variant o::some(move result):
            switch (move result) {
            case variant std.async::broadcast_result::received(move text):
                try {
                    note = change_note(&id, &granted, &board, text);
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                }
            case variant std.async::broadcast_result::lagged(move count):
                count as void;
                lagged = true;
            case variant std.async::broadcast_result::closed: open = false;
            }
        case variant o::none: open = false;
        }
        switch (move note) {
        case variant o::some(move item):
            task_scope(1) io { open = await deliver(&out, move item); }
        case variant o::none: break;
        }
        if (lagged == true && open == true) {
            task_scope(1) io { open = await send_missed(&out, &id, &granted, &board); }
        }
    }
    drop events;
    drop board;
}

/* The -32021 of a subscription to tasks from a client that does not declare the Tasks extension. */
protected outgoing refuse_tasks(const std.jsonrpc::request_id* id) throws std.alloc::alloc_error {
    try {
        return refuse(id, missing_tasks());
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return internal_error(id);
}

/* Refuses a subscription: one without a notifications object, or one that asks for tasks from a
   client that does not declare the Tasks extension (R-SLIB-MCP-0023). */
protected async void refuse_listen(outgoing reply, std.sync::sync_sender<outgoing> out) throws std.error::fault {
    task_scope(1) io {
        bool sent = await deliver(&out, move reply);
        sent as void;
    }
}

@generic<S: send & sync & unborrowed>
protected async void listen(arc server<S> host, admitted request, std.sync::sync_sender<outgoing> out, bool keepalive)
    throws std.error::fault {
    if (request.tasks == false && asks_tasks(&request.params) == true) {
        outgoing refused = refuse_tasks(&request.id);
        drop request;
        drop host;
        await refuse_listen(move refused, move out);
        return;
    }
    o<filter> granted = filter_of(&*host, &request.params);
    std.jsonrpc::request_id id = request.id.copy();
    drop request;
    switch (move granted) {
    case variant o::some(move chosen): await watch(move host, move id, move chosen, move out, keepalive);
    case variant o::none:
        await refuse_listen(invalid_params(&id, "Invalid params: notifications"), move out);
        drop id;
        drop host;
    }
}

/* ---- Dispatch ---- */

@generic<S: send & sync & unborrowed>
protected async outgoing answer_request(arc server<S> host, admitted request, std.sync::sync_sender<outgoing> out)
    throws std.error::fault {
    if (is_method(&request, "tools/call") == true) { return await call_tool(move host, move request, move out); }
    if (is_method(&request, "resources/read") == true) {
        return await read_resource(move host, move request, move out);
    }
    if (is_method(&request, "prompts/get") == true) { return await get_prompt(move host, move request, move out); }
    drop out;
    if (is_task_method(request.method) == true) {
        try {
            return answer_task_request(&*host, &request);
        } catch (std.json::error rejected) {
            (move rejected) as void;
        }
        return internal_error(&request.id);
    }
    if (is_method(&request, "completion/complete") == true) { return await complete(move host, move request); }
    if (is_method(&request, "resources/list") == true) { return await list_resources(move host, move request); }
    try {
        return listing(&*host, &request);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return internal_error(&request.id);
}

/* Answers a request that passed the shared checks: its notifications and then its response go
   to out; a subscription sends notifications until it ends. */
@generic<S: send & sync & unborrowed>
protected async void execute(arc server<S> host, admitted request, std.sync::sync_sender<outgoing> out, bool keepalive)
    throws std.error::fault {
    if (is_method(&request, "subscriptions/listen") == true) {
        await listen(move host, move request, move out, keepalive);
        return;
    }
    outgoing reply = await answer_request(move host, move request, std.sync::clone_sync_sender(&out));
    task_scope(1) io {
        bool sent = await deliver(&out, move reply);
        sent as void;
    }
}

/* ---- The runner of the tasks ---- */

/* The JSON-RPC error of a task that failed. */
protected std.json::value error_value(i64 code, str message, o<std.json::value> data)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    std.string::string spelled = f"{code}";
    std.json::number number = std.json::parse_number(spelled);
    put(&result, "code", std.json::from_number(&number));
    put_text(&result, "message", message);
    switch (move data) {
    case variant o::some(move value): put(&result, "data", move value);
    case variant o::none: break;
    }
    return move result;
}

protected std.json::value internal_error_value() throws std.alloc::alloc_error {
    try {
        return error_value(std.jsonrpc::internal_error, "Internal error", o::none);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.json::null();
}

/* Hands the stop of the program to the runner; a closed stop channel stops it too, by draining. */
protected async void forward_stop(std.sync::receiver<std.service::stop> stop, std.sync::sender<task_work> jobs)
    throws std.error::fault {
    o<std.service::stop> got = await stop.receive();
    std.service::stop kind = std.service::stop::drain;
    switch (got) {
    case variant o::some(value): kind = *value;
    case variant o::none: break;
    }
    std.sync::send_result<task_work> sent = std.sync::send(&jobs, task_work::stop(kind));
    drop sent;
}

/* The call that a handler of a task receives in a round. */
protected call task_call(const task_job* job, input given, const (arc task_board)* board) throws std.json::error, std.alloc::alloc_error {
    task_link link = {.board = std.arc::clone(board), .id = std.string::from_str(job->id)};
    return call {.name = std.string::from_str(job->name), .arguments = copy_value(&job->arguments),
                 .input = move given,
                 .progress = progress {.token = o::none, .sender = o::none, .last = 0.0f64, .started = false,
                                       .link = o::some(move link)}};
}

/* What a round of the handler of a task came to: its outcome, a stop, or a fault. */
protected enum round_end { outcome(tool_outcome), stopped, faulted };

/* Runs the handler of a task once, next to its stop signal. */
@generic<S: send & sync & unborrowed>
protected async round_end run_round(arc server<S> host, usize tool, call request, std.async::notify stop)
    throws std.error::fault {
    auto handler = host->tools[tool].handler;
    o<round_end> ended = o::none;
    try {
        task_scope(2) work {
            auto job = handler(std.arc::clone(&host->state), move request);
            auto halt = stop.notified();
            select (work) {
            case tool_outcome got = await move job:
                o<round_end> old = core::replace(&ended, o::some(round_end::outcome(move got)));
                drop old;
            case await move halt:
                o<round_end> old = core::replace(&ended, o::some(round_end::stopped));
                drop old;
            }
            work.cancel_all();
        }
    } catch (std.error::fault rejected) {
        rejected as void;
        return round_end::faulted;
    }
    switch (move ended) {
    case variant o::some(move value): return move value;
    case variant o::none: break;
    }
    return round_end::faulted;
}

/* Waits for the answers of a round or for a stop: true when the answers came. */
protected async bool wait_answers(std.async::notify wake, std.async::notify stop) throws std.error::fault {
    bool answered = false;
    task_scope(2) wait {
        auto answers = wake.notified();
        auto halt = stop.notified();
        select (wait) {
        case await move answers: answered = true;
        case await move halt: break;
        }
        wait.cancel_all();
    }
    return answered;
}

/* The -32021 of a task whose handler asked for a mode of input that its client did not declare. */
protected std.json::value missing_modes_error(const input_required* asked, bool form, bool url)
    throws std.json::error, std.alloc::alloc_error {
    bool need_form = false;
    bool need_url = false;
    for (usize index = 0usize; index < len(asked->requests); index += 1usize) {
        switch (asked->requests[index]) {
        case variant elicitation::form(item):
            item as void;
            need_form = form == false;
        case variant elicitation::url(item):
            item as void;
            need_url = url == false;
        }
    }
    if (need_form == false && need_url == false) { return std.json::null(); }
    refusal problem = missing_modes(need_form, need_url);
    o<std.json::value> data = core::replace(&problem.data, o::none);
    return error_value(problem.code, problem.message, move data);
}

protected input empty_input(bool form, bool url) {
    return input {.keys = std.array::create::<std.string::string>(), .answers = std.array::create::<answer>(),
                  .state = o::none, .form = form, .url = url};
}

/* What follows a round of a task: true when the handler runs again with the input put in
   pending. */
@scoped
protected async bool settle_round(const task_board* board, const task_job* job, const task_signals* signals,
                                  round_end ended, (o<input>)* pending) throws std.error::fault {
    str id = job->id;
    switch (move ended) {
    case variant round_end::stopped:
        board_finish(board, id, task_status::cancelled, std.json::null());
        return false;
    case variant round_end::faulted:
        board_finish(board, id, task_status::failed, internal_error_value());
        return false;
    case variant round_end::outcome(move outcome):
        switch (move outcome) {
        case variant tool_outcome::complete(move result):
            o<std.json::value> written = o::none;
            try {
                std.json::value made = to_value(&result);
                put_text(&made, "resultType", "complete");
                o<std.json::value> old = core::replace(&written, o::some(move made));
                drop old;
            } catch (std.json::error rejected) {
                (move rejected) as void;
            }
            switch (move written) {
            case variant o::some(move value): board_finish(board, id, task_status::completed, move value);
            case variant o::none: board_finish(board, id, task_status::failed, internal_error_value());
            }
            return false;
        case variant tool_outcome::needs_input(move asked):
            std.json::value missing = std.json::null();
            try {
                std.json::value found = missing_modes_error(&asked, job->form, job->url);
                std.json::value old = core::replace(&missing, move found);
                drop old;
            } catch (std.json::error rejected) {
                (move rejected) as void;
            }
            if (std.json::kind(&missing) != std.json::value_kind::null) {
                board_finish(board, id, task_status::failed, move missing);
                return false;
            }
            drop missing;
            if (core::atomic_load(&board->closing, core::memory_order::acquire) == 1u32) {
                board_finish(board, id, task_status::cancelled, std.json::null());
                return false;
            }
            bool waits = false;
            try {
                core::replace(&waits, board_ask(board, id, &asked)) as void;
            } catch (std.json::error rejected) {
                (move rejected) as void;
            }
            if (waits == false) {
                board_finish(board, id, task_status::failed, internal_error_value());
                return false;
            }
            bool answered = await wait_answers(signals->wake.clone(), signals->stop.clone());
            if (answered == false) {
                board_finish(board, id, task_status::cancelled, std.json::null());
                return false;
            }
            o<input> next = board_take(board, id, job->form, job->url);
            switch (move next) {
            case variant o::some(move value):
                o<input> old = core::replace(pending, o::some(move value));
                drop old;
                return true;
            case variant o::none: return false;
            }
        }
    }
    return false;
}

/* R-SLIB-MCP-0022: runs one task: the handler of its tool runs in rounds. A complete outcome
   completes the task with the result; needs_input makes the task wait for the answers of its
   client (input_required), after which the handler runs again with them; a stop cancels it and a
   fault fails it with -32603. */
@generic<S: send & sync & unborrowed>
protected async void run_rounds(arc server<S> host, arc task_board board, task_job job, task_signals signals)
    throws std.error::fault {
    o<input> pending = o::some(empty_input(job.form, job.url));
    bool going = true;
    while (going == true) {
        o<input> taken = core::replace(&pending, o::none);
        input given = empty_input(job.form, job.url);
        switch (move taken) {
        case variant o::some(move value):
            input old = core::replace(&given, move value);
            drop old;
        case variant o::none: break;
        }
        o<call> prepared = o::none;
        try {
            call made = task_call(&job, move given, &board);
            o<call> old = core::replace(&prepared, o::some(move made));
            drop old;
        } catch (std.json::error rejected) {
            (move rejected) as void;
        }
        switch (move prepared) {
        case variant o::some(move request):
            round_end ended = await run_round(std.arc::clone(&host), job.tool, move request, signals.stop.clone());
            const task_board* shared = &*board;
            task_scope(1) io { going = await settle_round(shared, &job, &signals, move ended, &pending); }
        case variant o::none:
            board_finish(&*board, job.id, task_status::failed, internal_error_value());
            going = false;
        }
    }
}

/* R-SLIB-MCP-0022: runs one task of the board of a server. */
@generic<S: send & sync & unborrowed>
protected async void run_task(arc server<S> host, task_job job) throws std.error::fault {
    o<arc task_board> found = shared_board(&*host);
    switch (move found) {
    case variant o::some(move board):
        o<task_signals> signals = board_signals(&*board, job.id);
        switch (move signals) {
        case variant o::some(move signal): await run_rounds(move host, move board, move job, move signal);
        case variant o::none:
            drop board;
            drop host;
            drop job;
        }
    case variant o::none:
        drop host;
        drop job;
    }
}

/* Tells the tasks that wait for input to stop: the runner drains. */
protected void stop_waiting_in(const array<task_entry>* entries) {
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if ((*entries)[index].status == task_status::input_required) { (*entries)[index].stop.notify_one(); }
    }
}

protected void stop_waiting(const task_board* board) {
    std.sync::lock_result<array<task_entry>> locked = std.sync::lock(&board->entries);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard): stop_waiting_in(std.sync::mutex_guard_ref(&guard));
    case variant std.sync::lock_result::poisoned(move guard): stop_waiting_in(std.sync::mutex_guard_ref(&guard));
    case variant std.sync::lock_result::would_deadlock: break;
    }
}

/* Starts the tasks that the channel of the board brings, at most 64 at once, until the stop:
   drain lets running tasks finish and stops those that wait for input, cancel cancels them all. */
@generic<S: send & sync & unborrowed>
protected async void run_jobs(arc server<S> host, arc task_board board, std.sync::receiver<task_work> jobs)
    throws std.error::fault {
    bool cancelling = false;
    task_scope(64) running {
        bool more = true;
        while (more == true) {
            o<task_work> next = await jobs.receive();
            switch (move next) {
            case variant o::some(move work):
                switch (move work) {
                case variant task_work::start(move job):
                    await running.vacancy();
                    auto member_task = run_task(std.arc::clone(&host), move job);
                    std.async::detach(move member_task);
                case variant task_work::stop(move kind):
                    more = false;
                    if (kind == std.service::stop::cancel) { cancelling = true; }
                }
            case variant o::none: more = false;
            }
        }
        core::atomic_exchange(&board->running, 0u32, core::memory_order::release) as void;
        core::atomic_exchange(&board->closing, 1u32, core::memory_order::release) as void;
        if (cancelling == true) {
            running.cancel_all();
        } else {
            stop_waiting(&*board);
        }
        await running.all();
    }
}

/* The receiver of the channel of a board, taken by the one runner. */
protected o<std.sync::receiver<task_work>> take_inbox(const task_board* board) {
    std.sync::lock_result<o<std.sync::receiver<task_work>>> locked = std.sync::lock(&board->inbox);
    switch (move locked) {
    case variant std.sync::lock_result::locked(move guard):
        return core::replace(std.sync::mutex_guard_mut(&guard), o::none);
    case variant std.sync::lock_result::poisoned(move guard):
        return core::replace(std.sync::mutex_guard_mut(&guard), o::none);
    case variant std.sync::lock_result::would_deadlock: break;
    }
    return o::none;
}

/* R-SLIB-MCP-0021: runs the tasks of a server until the stop of the program: the calls of its
   task tools from clients that declare the Tasks extension become tasks only while it runs. With
   std.service::stop::drain the running tasks finish and those that wait for input are cancelled;
   with cancel every task is cancelled; a closed stop channel drains. A server has one runner; it
   returns at once for a server without task tools or with another runner. */
/* The runner of the tasks of a board, with the receiver of its channel. */
@generic<S: send & sync & unborrowed>
protected async void run_with(arc server<S> host, arc task_board board, std.sync::receiver<task_work> jobs,
                              std.sync::receiver<std.service::stop> stop) throws std.error::fault {
    core::atomic_exchange(&board->closing, 0u32, core::memory_order::release) as void;
    core::atomic_exchange(&board->running, 1u32, core::memory_order::release) as void;
    std.sync::sender<task_work> relay = std.sync::clone_sender(&board->jobs);
    task_scope(2) control {
        auto forwarding = forward_stop(move stop, move relay);
        await run_jobs(move host, std.arc::clone(&board), move jobs);
        control.cancel_all();
    }
}

@generic<S: send & sync & unborrowed>
async void run_tasks(arc server<S> host, std.sync::receiver<std.service::stop> stop) throws std.error::fault {
    o<arc task_board> found = shared_board(&*host);
    switch (move found) {
    case variant o::some(move board):
        o<std.sync::receiver<task_work>> taken = take_inbox(&*board);
        switch (move taken) {
        case variant o::some(move jobs): await run_with(move host, move board, move jobs, move stop);
        case variant o::none:
            drop board;
            drop host;
            drop stop;
        }
    case variant o::none:
        drop host;
        drop stop;
    }
}

/* ---- stdio ---- */

/* A request in flight on a stream: its id, the notification that ends it, whether it is a
   subscription and whether the server is closing it. */
protected struct inflight {
    std.jsonrpc::request_id id;
    std.async::notify cancel;
    bool listen;
    bool closing;
    u64 serial;
};

/* Writes each message on its own line until every sender is gone; keep-alives are skipped. */
@generic<W: std.stream::Writer & unborrowed>
@scoped
protected async void write_lines(const W* output, std.sync::receiver<outgoing> receiver) throws std.error::fault {
    while (true) {
        o<outgoing> next = o::none;
        task_scope(1) io {
            o<outgoing> got = await receiver.receive();
            o<outgoing> old = core::replace(&next, move got);
            drop old;
        }
        switch (move next) {
        case variant o::none: return;
        case variant o::some(move item):
            o<std.jsonrpc::message> message = core::replace(&item.message, o::none);
            switch (move message) {
            case variant o::some(move value):
                try {
                    task_scope(1) io { await std.jsonrpc::write_message(output, &value); }
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                }
            case variant o::none: break;
            }
        }
    }
}

@scoped
protected async void register(const (std.async::mutex<array<inflight>>)* table, inflight entry)
    throws std.error::fault {
    std.async::mutex_guard<array<inflight>> guard = await table->lock();
    append(std.async::mutex_guard_mut(&guard), move entry);
}

/* Removes the entry of a request and returns whether the server was closing it. */
protected async bool forget(std.async::mutex<array<inflight>> table, u64 serial) throws std.error::fault {
    std.async::mutex_guard<array<inflight>> guard = await table.lock();
    array<inflight>* entries = std.async::mutex_guard_mut(&guard);
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if ((*entries)[index].serial == serial) {
            bool closing = (*entries)[index].closing;
            o<inflight> removed = entries->remove(index);
            drop removed;
            return closing;
        }
    }
    return false;
}

/* Ends the requests of an id that the client cancelled. */
@scoped
protected async void cancel_requests(const (std.async::mutex<array<inflight>>)* table, const std.jsonrpc::request_id* id)
    throws std.error::fault {
    std.async::mutex_guard<array<inflight>> guard = await table->lock();
    const array<inflight>* entries = std.async::mutex_guard_ref(&guard);
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if (std.cmp::is_equal(&(*entries)[index].id, id) == true) { (*entries)[index].cancel.notify_one(); }
    }
}

/* Closes every subscription: each ends with its graceful result. */
@scoped
protected async void close_subscriptions(const (std.async::mutex<array<inflight>>)* table) throws std.error::fault {
    std.async::mutex_guard<array<inflight>> guard = await table->lock();
    array<inflight>* entries = std.async::mutex_guard_mut(&guard);
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if ((*entries)[index].listen == true) {
            (*entries)[index].closing = true;
            (*entries)[index].cancel.notify_one();
        }
    }
}

/* The graceful end of a subscription on stdio: its result, then notifications/cancelled. */
protected void closing_notes_into(array<outgoing>* notes, const implementation* info, const std.jsonrpc::request_id* id)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    put_text(&result, "resultType", "complete");
    std.json::value meta = std.json::object();
    put(&meta, key_subscription, id->to_json());
    put(&meta, key_server, to_value(info));
    put(&result, "_meta", move meta);
    append(notes, success(id, move result));
    std.json::value params = std.json::object();
    put(&params, "requestId", id->to_json());
    std.jsonrpc::notification note = {.method = std.string::from_str("notifications/cancelled"),
                                      .params = o::some(move params)};
    append(notes, outgoing {.message = o::some(std.jsonrpc::message::notification(move note)), .status = 200u16});
}

protected array<outgoing> closing_notes(const implementation* info, const std.jsonrpc::request_id* id)
    throws std.alloc::alloc_error {
    array<outgoing> notes = std.array::create::<outgoing>();
    try {
        closing_notes_into(&notes, info, id);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return move notes;
}


/* Runs one request next to the notification that cancels it; a subscription that the server
   closes ends with its graceful result. */
@generic<S: send & sync & unborrowed>
protected async void run_request(arc server<S> host, admitted request, std.sync::sync_sender<outgoing> out,
                                 std.async::notify cancel, std.async::mutex<array<inflight>> table, u64 serial)
    throws std.error::fault {
    std.jsonrpc::request_id id = request.id.copy();
    bool listening = is_method(&request, "subscriptions/listen");
    bool stopped = false;
    try {
        task_scope(2) work {
            auto job = execute(std.arc::clone(&host), move request, std.sync::clone_sync_sender(&out), false);
            auto stop = cancel.notified();
            select (work) {
            case await move job: break;
            case await move stop: stopped = true;
            }
            work.cancel_all();
        }
    } catch (std.error::fault rejected) {
        rejected as void;
    }
    bool closing = await forget(std.async::clone_mutex(&table), serial);
    bool graceful = listening;
    if (closing == false || stopped == false) { graceful = false; }
    if (graceful == true) {
        array<outgoing> notes = closing_notes(&host->info, &id);
        bool open = true;
        bool more = true;
        while (open == true && more == true) {
            o<outgoing> next = notes.remove(0usize);
            switch (move next) {
            case variant o::some(move item):
                task_scope(1) io { open = await deliver(&out, move item); }
            case variant o::none: more = false;
            }
        }
        drop notes;
    }
    drop id;
}

/* What a line of a stream asks of the server: a reply at once, the cancellation of requests,
   a request to run, or nothing. */
protected enum incoming { reply(outgoing), cancel(std.jsonrpc::request_id), start(admitted), ignore };

protected outgoing invalid_request() throws std.alloc::alloc_error {
    return refused_reply(o::none, refusal_of(std.jsonrpc::invalid_request, "Invalid Request", 400u16));
}

@generic<S: send & sync & unborrowed>
protected incoming classify(const server<S>* host, const u8[] line) throws std.alloc::alloc_error {
    std.jsonrpc::message parsed = std.jsonrpc::message::notification(std.jsonrpc::notification {
        .method = std.string::create(), .params = o::none});
    try {
        std.jsonrpc::message read_message = std.jsonrpc::parse(line);
        std.jsonrpc::message old = core::replace(&parsed, move read_message);
        drop old;
    } catch (std.jsonrpc::rpc_error rejected) {
        std.jsonrpc::error_response answer_error = std.jsonrpc::error_response::of(&rejected);
        (move rejected) as void;
        return incoming::reply(outgoing {.message = o::some(std.jsonrpc::message::failure(move answer_error)),
                                         .status = 400u16});
    }
    switch (move parsed) {
    case variant std.jsonrpc::message::request(move request):
        std.jsonrpc::request_id id = request.id.copy();
        o<str> no_header = o::none;
        try {
            admission decided = admit(host, move request, no_header);
            switch (move decided) {
            case variant admission::accepted(move admitted_request):
                drop id;
                return incoming::start(move admitted_request);
            case variant admission::refused(move problem): return incoming::reply(refuse(&id, move problem));
            }
        } catch (std.json::error rejected) {
            (move rejected) as void;
        }
        return incoming::reply(internal_error(&id));
    case variant std.jsonrpc::message::notification(move note):
        if (std.bytes::equal(note.method, "notifications/cancelled") == false) { return incoming::ignore; }
        switch (note.params) {
        case variant o::some(params):
            switch (member(params, "requestId")) {
            case variant o::some(value):
                o<std.jsonrpc::request_id> id = std.jsonrpc::request_id::from_json(*value);
                switch (move id) {
                case variant o::some(move chosen): return incoming::cancel(move chosen);
                case variant o::none: break;
                }
            case variant o::none: break;
            }
        case variant o::none: break;
        }
        return incoming::ignore;
    case variant std.jsonrpc::message::result(move item):
        drop item;
        return incoming::reply(invalid_request());
    case variant std.jsonrpc::message::failure(move item):
        drop item;
        return incoming::reply(invalid_request());
    }
}

/* Reads the lines of the stream and starts a task for each request, at most 64 at once; at the
   end of the stream every subscription is closed and every request finishes. */
@generic<S: send & sync & unborrowed, R: std.stream::Reader & unborrowed>
@scoped
protected async void read_requests(arc server<S> host, std.bufio::reader<R>* lines, std.sync::sync_sender<outgoing> out,
                                   const (std.async::mutex<array<inflight>>)* table) throws std.error::fault {
    u64 serial = 0u64;
    task_scope(64) requests {
        bool reading = true;
        while (reading == true) {
            o<bytes> next = o::none;
            task_scope(1) io {
                o<bytes> got = await std.jsonrpc::read_line(lines);
                o<bytes> old = core::replace(&next, move got);
                drop old;
            }
            switch (move next) {
            case variant o::none: reading = false;
            case variant o::some(move line):
                incoming action = classify(&*host, line.as_slice());
                switch (move action) {
                case variant incoming::reply(move item):
                    task_scope(1) io {
                        bool sent = await deliver(&out, move item);
                        sent as void;
                    }
                case variant incoming::cancel(move id):
                    task_scope(1) io { await cancel_requests(table, &id); }
                case variant incoming::start(move request):
                    serial += 1u64;
                    std.async::notify token = std.async::notify_new();
                    inflight entry = {.id = request.id.copy(), .cancel = token.clone(),
                                      .listen = is_method(&request, "subscriptions/listen"), .closing = false,
                                      .serial = serial};
                    task_scope(1) io { await register(table, move entry); }
                    await requests.vacancy();
                    auto member_task = run_request(std.arc::clone(&host), move request, std.sync::clone_sync_sender(&out),
                                                   move token, std.async::clone_mutex(table), serial);
                    std.async::detach(move member_task);
                case variant incoming::ignore: break;
                }
            }
        }
        task_scope(1) io { await close_subscriptions(table); }
        await requests.all();
    }
}

/* R-SLIB-MCP-0014: serves MCP over a reader and a writer with the framing of stdio: one
   JSON-RPC message per line. Requests run concurrently, at most 64 at once, and each response
   is written as one line; notifications/cancelled ends the requests of its id. It returns when
   the input ends, after every subscription got its graceful result and every request finished. */
@generic<S: send & sync & unborrowed, R: std.stream::Reader & unborrowed, W: std.stream::Writer & unborrowed>
async void serve_streams(arc server<S> host, R input, W output) throws std.error::fault {
    usize capacity = host->settings.max_message;
    std.sync::sync_channel<outgoing> factory = std.sync::sync_channel::<outgoing>(64usize);
    std.sync::sync_sender<outgoing> sender = std.sync::sync_sender(&factory);
    std.sync::receiver<outgoing> receiver = std.sync::sync_receiver(move factory);
    std.bufio::reader<R> lines = std.bufio::reader<R>::create(move input, capacity);
    std.async::mutex<array<inflight>> table = std.async::mutex_new(std.array::create::<inflight>());
    task_scope(2) session {
        auto writing = write_lines(&output, move receiver);
        auto reading = read_requests(std.arc::clone(&host), &lines, move sender, &table);
        await move reading;
        await move writing;
    }
}

/* R-SLIB-MCP-0014: serves MCP on standard input and output. */
@generic<S: send & sync & unborrowed>
async void serve_stdio(arc server<S> host) throws std.error::fault {
    await serve_streams(move host, std.io::stdin(), std.io::stdout());
}

/* ---- Streamable HTTP ---- */

/* The value of an MCP header: a =?base64?...?= value decoded as UTF-8, any other value as it
   is; none when it has characters outside visible ASCII, space and tab, or is no valid
   base64 of UTF-8. */
protected o<std.string::string> header_value(str spelled) throws std.alloc::alloc_error {
    const u8[] bytes = spelled;
    if (len(bytes) >= 11usize && std.text::starts_with(spelled, "=?base64?") == true &&
        std.text::ends_with(spelled, "?=") == true) {
        try {
            bytes decoded = std.encoding::decode_base64(piece(spelled, 9usize, len(bytes) - 2usize));
            str text = core::validate_utf8(decoded.as_slice());
            return o::some(std.string::from_str(text));
        } catch (std.convert::parse_error rejected) {
            rejected as void;
        } catch (core::utf8_error rejected) {
            rejected as void;
        }
        return o::none;
    }
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 value = bytes[index];
        if (value != 9u8 && (value < 32u8 || value > 126u8)) { return o::none; }
    }
    return o::some(std.string::from_str(spelled));
}

/* Whether a header holds the value of a JSON argument: the same string, the same number, or
   true or false for a Boolean. */
protected bool header_matches(str header, const std.json::value* argument) {
    std.json::value_kind kind = std.json::kind(argument);
    if (kind == std.json::value_kind::string) { return std.bytes::equal(header, std.json::text(argument)); }
    if (kind == std.json::value_kind::boolean) {
        if (std.json::boolean(argument) == true) { return std.bytes::equal(header, "true"); }
        return std.bytes::equal(header, "false");
    }
    if (kind == std.json::value_kind::number) {
        try {
            f64 left = std.convert::parse_f64(header);
            f64 right = std.convert::parse_f64(std.json::text(argument));
            return left == right;
        } catch (std.convert::parse_error rejected) {
            rejected as void;
        }
    }
    return false;
}

/* An argument that a tool mirrors into a header: the header name and the path of property names
   that leads to it. */
protected struct mirror { std.string::string header; array<std.string::string> path; };

protected array<std.string::string> copy_path(const array<std.string::string>* path) throws std.alloc::alloc_error {
    array<std.string::string> result = std.array::create::<std.string::string>();
    for (usize index = 0usize; index < len(*path); index += 1usize) { add_text(&result, (*path)[index]); }
    return move result;
}

/* The schema of the property at a path of property names, none when absent. */
protected o<const std.json::value*> property_at(const std.json::value* schema, const array<std.string::string>* path) {
    o<const std.json::value*> current = o::some(schema);
    for (usize index = 0usize; index < len(*path); index += 1usize) {
        switch (current) {
        case variant o::some(value):
            switch (member(*value, "properties")) {
            case variant o::some(properties): current = member(*properties, (*path)[index]);
            case variant o::none: return o::none;
            }
        case variant o::none: return o::none;
        }
    }
    return current;
}

/* The x-mcp-header annotations of the properties that a schema reaches through properties
   alone, at most 32 levels deep. */
protected array<mirror> mirrors_of(const std.json::value* schema) throws std.alloc::alloc_error {
    array<mirror> found = std.array::create::<mirror>();
    array<array<std.string::string>> pending = std.array::create::<array<std.string::string>>();
    append(&pending, std.array::create::<std.string::string>());
    bool more = true;
    while (more == true) {
        o<array<std.string::string>> next = pending.pop();
        switch (move next) {
        case variant o::some(move prefix):
            switch (property_at(schema, &prefix)) {
            case variant o::some(node):
                switch (member(*node, "properties")) {
                case variant o::some(properties):
                    for (usize index = 0usize; index < std.json::len(*properties); index += 1usize) {
                        str key = std.json::key_at(*properties, index);
                        array<std.string::string> path = copy_path(&prefix);
                        add_text(&path, key);
                        switch (member(*properties, key)) {
                        case variant o::some(property):
                            switch (member_text(*property, "x-mcp-header")) {
                            case variant o::some(name):
                                append(&found, mirror {.header = std.string::from_str(*name), .path = copy_path(&path)});
                            case variant o::none: break;
                            }
                        case variant o::none: break;
                        }
                        if (len(path) < 32usize) {
                            append(&pending, move path);
                        } else {
                            drop path;
                        }
                    }
                case variant o::none: break;
                }
            case variant o::none: break;
            }
            drop prefix;
        case variant o::none: more = false;
        }
    }
    drop pending;
    return move found;
}

/* The argument at a path of property names, none when absent. */
protected o<const std.json::value*> argument_at(const std.json::value* arguments, const array<std.string::string>* path) {
    o<const std.json::value*> current = o::some(arguments);
    for (usize index = 0usize; index < len(*path); index += 1usize) {
        switch (current) {
        case variant o::some(value): current = member(*value, (*path)[index]);
        case variant o::none: return o::none;
        }
    }
    return current;
}

/* Whether the Mcp-Param headers of a tools/call request match its arguments: each mirrored
   argument that is present and not null has its header, equal after decoding. */
protected bool params_match(const std.json::value* schema, const std.json::value* params, const std.http::headers* fields)
    throws std.alloc::alloc_error {
    array<mirror> found = mirrors_of(schema);
    std.json::value empty = std.json::null();
    const std.json::value* arguments = &empty;
    switch (member(params, "arguments")) {
    case variant o::some(given): arguments = *given;
    case variant o::none: break;
    }
    for (usize index = 0usize; index < len(found); index += 1usize) {
        switch (argument_at(arguments, &found[index].path)) {
        case variant o::some(value):
            if (std.json::kind(*value) == std.json::value_kind::null) { continue; }
            std.string::string name = std.string::from_str("Mcp-Param-");
            std.string::append_str(&name, found[index].header);
            switch (fields->get(name)) {
            case variant o::some(spelled):
                o<std.string::string> decoded = header_value(*spelled);
                switch (move decoded) {
                case variant o::some(move text):
                    if (header_matches(text, *value) == false) { return false; }
                case variant o::none: return false;
                }
            case variant o::none: return false;
            }
        case variant o::none: break;
        }
    }
    return true;
}

/* Whether a media type, without its parameters, is the given one. */
protected bool media_is(str value, str wanted) {
    const u8[] bytes = value;
    usize end = 0usize;
    while (end < len(bytes) && bytes[end] != 59u8) { end += 1usize; }
    return std.text::equal_ignore_ascii_case(std.text::trim(piece(value, 0usize, end)), wanted);
}

protected bool content_type_ok(const std.http::headers* fields) {
    switch (fields->get("Content-Type")) {
    case variant o::some(value): return media_is(*value, "application/json");
    case variant o::none: return true;
    }
}

/* Whether an Accept field admits JSON or an event stream; a request without one does. */
protected bool accept_ok(const std.http::headers* fields) {
    switch (fields->get("Accept")) {
    case variant o::some(value):
        for (str range in std.text::split(*value, ",")) {
            if (media_is(range, "application/json") == true || media_is(range, "text/event-stream") == true ||
                media_is(range, "application/*") == true || media_is(range, "text/*") == true ||
                media_is(range, "*/*") == true) {
                return true;
            }
        }
        return false;
    case variant o::none: return true;
    }
}

/* Whether a host is local: localhost, 127.0.0.1 or ::1. */
protected bool local_host(str host) {
    return std.text::equal_ignore_ascii_case(host, "localhost") == true || std.bytes::equal(host, "127.0.0.1") == true ||
           std.bytes::equal(host, "::1") == true || std.bytes::equal(host, "[::1]") == true;
}

/* Whether a request may come from its Origin: none, an allowed origin, a local host or the
   authority of the request. */
@generic<S: send & sync & unborrowed>
protected bool origin_allowed(const server<S>* host, const std.http::request* incoming) throws std.alloc::alloc_error {
    switch (incoming->headers.get("Origin")) {
    case variant o::some(origin):
        for (usize index = 0usize; index < len(host->origins); index += 1usize) {
            if (std.bytes::equal(host->origins[index], *origin) == true) { return true; }
        }
        try {
            std.url::url parsed = std.url::parse(*origin);
            if (local_host(parsed.host()) == true) { return true; }
            switch (incoming->headers.get("Host")) {
            case variant o::some(name):
                std.string::string authority = parsed.authority_text();
                return std.text::equal_ignore_ascii_case(authority, *name);
            case variant o::none: break;
            }
        } catch (std.url::url_error rejected) {
            rejected as void;
        }
        return false;
    case variant o::none: return true;
    }
}

/* The URL of the Protected Resource Metadata of a resource (RFC 9728): the origin of the
   resource, the well-known path, then the path of the resource. */
protected std.string::string metadata_url(str resource) throws std.alloc::alloc_error {
    try {
        std.url::url parsed = std.url::parse(resource);
        std.string::string text = std.string::from_str(parsed.scheme());
        std.string::append_str(&text, "://");
        std.string::string authority = parsed.authority_text();
        std.string::append_str(&text, authority);
        std.string::append_str(&text, "/.well-known/oauth-protected-resource");
        str path = parsed.path();
        if (std.bytes::equal(path, "/") == false) { std.string::append_str(&text, path); }
        return move text;
    } catch (std.url::url_error rejected) {
        rejected as void;
    }
    return std.string::from_str(resource);
}

/* The WWW-Authenticate field of a challenge: the metadata, the scopes and an error when not
   empty. */
protected std.string::string challenge_text(const protection* guard, str error_code) throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str("Bearer resource_metadata=\"");
    std.string::string url = metadata_url(guard->resource);
    std.string::append_str(&text, url);
    std.string::append_str(&text, "\"");
    if (len(guard->scopes) != 0usize) {
        std.string::append_str(&text, ", scope=\"");
        for (usize index = 0usize; index < len(guard->scopes); index += 1usize) {
            if (index != 0usize) { std.string::append_str(&text, " "); }
            std.string::append_str(&text, guard->scopes[index]);
        }
        std.string::append_str(&text, "\"");
    }
    const u8[] wanted = error_code;
    if (len(wanted) != 0usize) {
        std.string::append_str(&text, ", error=\"");
        std.string::append_str(&text, error_code);
        std.string::append_str(&text, "\"");
    }
    return move text;
}

/* The bearer token of a request, none when its Authorization field carries none. */
protected o<str> bearer_of(const std.http::headers* fields) {
    switch (fields->get("Authorization")) {
    case variant o::some(value):
        str text = std.text::trim(*value);
        const u8[] bytes = text;
        if (len(bytes) > 7usize && std.text::equal_ignore_ascii_case(piece(text, 0usize, 7usize), "Bearer ") == true) {
            str token = std.text::trim(piece(text, 7usize, len(bytes)));
            const u8[] token_bytes = token;
            if (len(token_bytes) != 0usize) { return o::some(token); }
        }
    case variant o::none: break;
    }
    return o::none;
}

/* The challenge of a request to a protected server: none when its token allows the method. */
@generic<S: send & sync & unborrowed>
protected o<challenge_head> authorize(const server<S>* host, const std.http::headers* fields, str method)
    throws std.alloc::alloc_error {
    switch (host->guard) {
    case variant o::some(guard):
        switch (bearer_of(fields)) {
        case variant o::some(token):
            auto checker = host->checker;
            switch (checker) {
            case variant o::some(chosen):
                auto check = *chosen;
                access decision = check(&*host->state, *token, method);
                switch (decision) {
                case access::allowed: return o::none;
                case access::invalid:
                    return o::some(challenge_head {.status = 401u16, .challenge = challenge_text(guard, "invalid_token")});
                case access::insufficient:
                    return o::some(challenge_head {.status = 403u16, .challenge = challenge_text(guard, "insufficient_scope")});
                }
            case variant o::none: return o::none;
            }
        case variant o::none: return o::some(challenge_head {.status = 401u16, .challenge = challenge_text(guard, "")});
        }
    case variant o::none: break;
    }
    return o::none;
}

/* What a POST asks of the server: a response without body, a JSON-RPC response of a status, or a
   request to run. */
protected enum post_plan { bare(u16), reply(outgoing), run(admitted), challenge(challenge_head) };

/* A 401 or 403 response with its WWW-Authenticate field. */
protected struct challenge_head { u16 status; std.string::string challenge; };

protected post_plan mismatch(const std.jsonrpc::request_id* id, str message) throws std.alloc::alloc_error {
    return post_plan::reply(refuse(id, refusal_of(header_mismatch, message, 400u16)));
}

protected bool names_target(str method) {
    return std.bytes::equal(method, "tools/call") == true || std.bytes::equal(method, "resources/read") == true ||
           std.bytes::equal(method, "prompts/get") == true || is_task_method(method) == true;
}

/* The checks of steps 1 to 13 of a POST: Origin (403), Content-Type (415), Accept (406), the
   message (-32700, -32600, 400), a notification (202), the MCP headers (-32020), the checks of
   admit, Mcp-Name against the body and the Mcp-Param headers of tools/call (-32020). */
@generic<S: send & sync & unborrowed>
protected post_plan plan_post(const server<S>* host, const std.http::request* incoming)
    throws std.json::error, std.alloc::alloc_error {
    if (origin_allowed(host, incoming) == false) { return post_plan::bare(403u16); }
    if (content_type_ok(&incoming->headers) == false) { return post_plan::bare(415u16); }
    if (accept_ok(&incoming->headers) == false) { return post_plan::bare(406u16); }
    o<challenge_head> anonymous = authorize(host, &incoming->headers, "");
    switch (move anonymous) {
    case variant o::some(move head):
        if (head.status == 401u16) { return post_plan::challenge(move head); }
        drop head;
    case variant o::none: break;
    }
    std.jsonrpc::message parsed = std.jsonrpc::message::notification(std.jsonrpc::notification {
        .method = std.string::create(), .params = o::none});
    try {
        std.jsonrpc::message read_message = std.jsonrpc::parse(incoming->body.as_slice());
        std.jsonrpc::message old = core::replace(&parsed, move read_message);
        drop old;
    } catch (std.jsonrpc::rpc_error rejected) {
        std.jsonrpc::error_response answer_error = std.jsonrpc::error_response::of(&rejected);
        (move rejected) as void;
        return post_plan::reply(outgoing {.message = o::some(std.jsonrpc::message::failure(move answer_error)),
                                          .status = 400u16});
    }
    switch (move parsed) {
    case variant std.jsonrpc::message::notification(move note):
        o<challenge_head> denied = authorize(host, &incoming->headers, note.method);
        drop note;
        switch (move denied) {
        case variant o::some(move head): return post_plan::challenge(move head);
        case variant o::none: break;
        }
        return post_plan::bare(202u16);
    case variant std.jsonrpc::message::result(move item):
        drop item;
        return post_plan::reply(invalid_request());
    case variant std.jsonrpc::message::failure(move item):
        drop item;
        return post_plan::reply(invalid_request());
    case variant std.jsonrpc::message::request(move message):
        o<challenge_head> denied = authorize(host, &incoming->headers, message.method);
        switch (move denied) {
        case variant o::some(move head):
            drop message;
            return post_plan::challenge(move head);
        case variant o::none: break;
        }
        std.jsonrpc::request_id id = message.id.copy();
        o<str> version = incoming->headers.get("MCP-Protocol-Version");
        if (present(version) == false) {
            drop message;
            return mismatch(&id, "Header mismatch: the MCP-Protocol-Version header is missing");
        }
        switch (incoming->headers.get("Mcp-Method")) {
        case variant o::some(sent):
            if (std.bytes::equal(*sent, message.method) == false) {
                drop message;
                return mismatch(&id, "Header mismatch: Mcp-Method does not match the method of the body");
            }
        case variant o::none:
            drop message;
            return mismatch(&id, "Header mismatch: the Mcp-Method header is missing");
        }
        bool named = names_target(message.method);
        if (named == true && present(incoming->headers.get("Mcp-Name")) == false) {
            drop message;
            return mismatch(&id, "Header mismatch: the Mcp-Name header is missing");
        }
        admission decided = admit(host, move message, version);
        switch (move decided) {
        case variant admission::refused(move problem): return post_plan::reply(refuse(&id, move problem));
        case variant admission::accepted(move request):
            if (named == true) {
                str key = "name";
                if (is_method(&request, "resources/read") == true) { key = "uri"; }
                if (is_task_method(request.method) == true) { key = "taskId"; }
                switch (member_text(&request.params, key)) {
                case variant o::some(body):
                    bool same = false;
                    switch (incoming->headers.get("Mcp-Name")) {
                    case variant o::some(spelled):
                        o<std.string::string> decoded = header_value(*spelled);
                        switch (move decoded) {
                        case variant o::some(move text): same = std.bytes::equal(text, *body);
                        case variant o::none: break;
                        }
                    case variant o::none: break;
                    }
                    if (same == false) {
                        drop request;
                        return mismatch(&id, "Header mismatch: Mcp-Name does not match the body");
                    }
                case variant o::none: break;
                }
            }
            if (is_method(&request, "tools/call") == true) {
                switch (member_text(&request.params, "name")) {
                case variant o::some(name):
                    for (usize index = 0usize; index < len(host->tools); index += 1usize) {
                        if (std.bytes::equal(host->tools[index].definition.name, *name) == true &&
                            params_match(&host->tools[index].definition.input_schema, &request.params,
                                         &incoming->headers) == false) {
                            drop request;
                            return mismatch(&id, "Header mismatch: an Mcp-Param header does not match its argument");
                        }
                    }
                case variant o::none: break;
                }
            }
            drop id;
            return post_plan::run(move request);
        }
    }
    return post_plan::bare(500u16);
}

protected std.http::response json_head(u16 status) throws std.alloc::alloc_error {
    std.http::response head = std.http::response::create(status);
    try {
        head.headers.add("Content-Type", "application/json");
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    return move head;
}

protected std.http::response stream_head() throws std.alloc::alloc_error {
    std.http::response head = std.http::response::create(200u16);
    try {
        head.headers.add("Content-Type", "text/event-stream");
        head.headers.add("Cache-Control", "no-cache");
        head.headers.add("X-Accel-Buffering", "no");
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    return move head;
}

protected bool is_response(const std.jsonrpc::message* message) {
    switch (*message) {
    case variant std.jsonrpc::message::result(item):
        item as void;
        return true;
    case variant std.jsonrpc::message::failure(item):
        item as void;
        return true;
    default: return false;
    }
}

protected o<std.string::string> encoded_text(const std.jsonrpc::message* message) throws std.alloc::alloc_error {
    try {
        return o::some(std.jsonrpc::encode(message));
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return o::none;
}

protected std.string::string text_or_empty(o<std.string::string> text) throws std.alloc::alloc_error {
    switch (move text) {
    case variant o::some(move value): return move value;
    case variant o::none: return std.string::create();
    }
}

/* Writes a JSON body with a status. */
@scoped
protected async void write_json(std.http::body_writer* writer, u16 status, str text) throws std.error::fault {
    std.http::response head = json_head(status);
    task_scope(1) io {
        bool started = await writer->start(move head);
        if (started == true) {
            bool sent = await writer->send_text(text);
            sent as void;
        }
    }
}

/* Writes one message of a request: a response that comes first as a JSON response of its
   status; otherwise an event stream whose events carry the messages up to the response, with a
   comment for a keep-alive. Returns whether more messages follow. */
@scoped
protected async bool forward_http(std.http::body_writer* writer, outgoing item, bool* streaming)
    throws std.error::fault {
    o<std.jsonrpc::message> taken = core::replace(&item.message, o::none);
    switch (move taken) {
    case variant o::none:
        if (*streaming == false) { return true; }
        task_scope(1) io { return await writer->send_text(": keep-alive\n\n"); }
    case variant o::some(move message):
        bool last = is_response(&message);
        o<std.string::string> encoded = encoded_text(&message);
        drop message;
        std.string::string text = text_or_empty(move encoded);
        if (std.string::len(&text) == 0usize) { return last == false; }
        if (last == true && *streaming == false) {
            task_scope(1) io { await write_json(writer, item.status, text); }
            return false;
        }
        if (*streaming == false) {
            *streaming = true;
            task_scope(1) io {
                bool started = await writer->start(stream_head());
                if (started == false) { return false; }
            }
        }
        bool more = last == false;
        std.string::string event = std.http::sse_event("", "", text);
        task_scope(1) io {
            bool alive = await writer->send_text(event);
            if (alive == false) { return false; }
        }
        return more;
    }
    return false;
}

/* Runs a request of a POST next to the writer of its response, which ends when the response is
   written or the client is gone; a subscription keeps its stream alive with comments. */
@generic<S: send & sync & unborrowed>
protected async void respond_http(arc server<S> host, admitted request, std.http::body_writer writer)
    throws std.error::fault {
    std.sync::sync_channel<outgoing> factory = std.sync::sync_channel::<outgoing>(16usize);
    std.sync::sync_sender<outgoing> sender = std.sync::sync_sender(&factory);
    std.sync::receiver<outgoing> receiver = std.sync::sync_receiver(move factory);
    bool streaming = false;
    bool open = true;
    task_scope(2) exchange {
        auto job = execute(std.arc::clone(&host), move request, move sender, true);
        while (open == true) {
            o<outgoing> next = await receiver.receive();
            switch (move next) {
            case variant o::some(move item):
                task_scope(1) io { open = await forward_http(&writer, move item, &streaming); }
            case variant o::none: open = false;
            }
        }
        exchange.cancel_all();
    }
    drop receiver;
}

protected async void write_head(std.http::body_writer writer, u16 status) throws std.error::fault {
    std.http::response head = std.http::response::create(status);
    task_scope(1) io {
        bool started = await writer.start(move head);
        started as void;
    }
}

protected async void write_challenge(std.http::body_writer writer, challenge_head head) throws std.error::fault {
    std.http::response reply_head = std.http::response::create(head.status);
    try {
        reply_head.headers.add("WWW-Authenticate", head.challenge);
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    task_scope(1) io {
        bool started = await writer.start(move reply_head);
        started as void;
    }
}

protected async void write_reply(std.http::body_writer writer, outgoing item) throws std.error::fault {
    bool streaming = false;
    task_scope(1) io {
        bool more = await forward_http(&writer, move item, &streaming);
        more as void;
    }
}

/* R-SLIB-MCP-0016: answers one POST to the endpoint. */
@generic<S: send & sync & unborrowed>
protected async void serve_post(arc server<S> host, std.http::request incoming, std.http::body_writer writer)
    throws std.error::fault {
    o<post_plan> planned = o::none;
    try {
        post_plan made = plan_post(&*host, &incoming);
        o<post_plan> old = core::replace(&planned, o::some(move made));
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    drop incoming;
    switch (move planned) {
    case variant o::some(move plan):
        switch (move plan) {
        case variant post_plan::bare(status):
            drop host;
            await write_head(move writer, *status);
        case variant post_plan::reply(move item):
            drop host;
            await write_reply(move writer, move item);
        case variant post_plan::run(move request): await respond_http(move host, move request, move writer);
        case variant post_plan::challenge(move head):
            drop host;
            await write_challenge(move writer, move head);
        }
    case variant o::none:
        drop host;
        await write_head(move writer, 500u16);
    }
}

/* R-SLIB-MCP-0020: the Protected Resource Metadata of a protected server; 404 for another. */
@generic<S: send & sync & unborrowed>
protected async std.http::response serve_metadata(arc server<S> host, std.http::request incoming)
    throws std.error::fault {
    drop incoming;
    switch (host->guard) {
    case variant o::some(guard):
        try {
            std.json::value document = std.json::object();
            put_text(&document, "resource", guard->resource);
            std.json::value servers = std.json::array();
            for (usize index = 0usize; index < len(guard->authorization_servers); index += 1usize) {
                std.json::append(&servers, std.json::from_string(guard->authorization_servers[index]));
            }
            put(&document, "authorization_servers", move servers);
            if (len(guard->scopes) != 0usize) {
                std.json::value scopes = std.json::array();
                for (usize index = 0usize; index < len(guard->scopes); index += 1usize) {
                    std.json::append(&scopes, std.json::from_string(guard->scopes[index]));
                }
                put(&document, "scopes_supported", move scopes);
            }
            std.json::value methods = std.json::array();
            std.json::append(&methods, std.json::from_string("header"));
            put(&document, "bearer_methods_supported", move methods);
            std.string::string text = std.json::stringify(&document);
            return std.http::response::json(200u16, text);
        } catch (std.json::error rejected) {
            (move rejected) as void;
        }
        return std.http::response::text(500u16, std.http::reason(500u16));
    case variant o::none: break;
    }
    return std.http::response::text(404u16, std.http::reason(404u16));
}

/* R-SLIB-MCP-0016: adds the MCP endpoint at path to a router whose state is the server. A POST
   carries one message: a request is answered with one JSON response, or with an event stream
   when notifications about it come first, as for progress and subscriptions/listen; a
   notification with 202. The router answers other methods on the path with 405 and Allow: POST.
   A browser request from an origin other than the server itself, localhost and the allowed ones
   gets 403. */
@generic<S: send & sync & unborrowed>
void route(std.http::router<server<S>>* routes, str path) throws std.http::http_error, std.alloc::alloc_error {
    routes->add_stream(std.http::method::post, path, serve_post::<S>);
    routes->add(std.http::method::get, "/.well-known/oauth-protected-resource", serve_metadata::<S>);
    if (std.bytes::equal(path, "/") == false) {
        std.string::string inserted = std.string::from_str("/.well-known/oauth-protected-resource");
        std.string::append_str(&inserted, path);
        routes->add(std.http::method::get, inserted, serve_metadata::<S>);
    }
}

/* ---- Client ---- */

/* R-SLIB-MCP-0017: the failures that a client meets itself, outside the codes of JSON-RPC: a
   request that took longer than its timeout, a connection that ended, and a server that broke
   the protocol. */
const i64 request_timeout = 1i64;
const i64 connection_closed = 2i64;
const i64 protocol_violation = 3i64;

/* R-SLIB-MCP-0020: a server over HTTP refused the token of the client (401), or the scopes of the
   token (403). */
const i64 unauthorized = 4i64;
const i64 forbidden = 5i64;

/* R-SLIB-MCP-0024: a task that a client followed was cancelled. */
const i64 task_cancelled = 6i64;

/* R-SLIB-MCP-0017: the settings of a client: the elicitation modes that it declares on the
   requests that may ask for input, the most rounds of a Multi Round-Trip Request, the time
   each request may take, the longest message and (R-SLIB-MCP-0024) whether it declares the
   Tasks extension on its requests. */
struct client_options {
    bool form = true;
    bool url = false;
    u32 rounds = 10u32;
    std.time::duration timeout = std.time::duration_from_seconds(60i64);
    usize max_message = 4194304usize;
    bool tasks = false;
};

/* A request in flight on a stream and where the messages about it go. */
protected struct waiter { std.jsonrpc::request_id id; std.sync::sync_sender<std.jsonrpc::message> sender; };

/* The two directions of a stream to a server and the requests in flight on it. */
protected struct stream_link {
    std.async::mutex<o<own dyn(std.stream::Writer)*>> output;
    std.async::mutex<std.bufio::reader<own dyn(std.stream::Reader)*>> input;
    std.async::mutex<array<waiter>> waiters;
};

/* The endpoint of a server over HTTP, the settings of the HTTP clients and the tools that the
   server listed, whose arguments the client mirrors into Mcp-Param headers. */
protected struct http_link {
    std.string::string endpoint;
    std.http::client_options web;
    o<arc std.tls::config> tls;
    std.async::mutex<array<tool>> tools;
    o<std.string::string> token;
};

/* R-SLIB-MCP-0017: a client of one server, over a stream with the framing of stdio or over
   Streamable HTTP. Its requests carry the protocol version, its capabilities and its identity
   in _meta; they may run concurrently. A stream client reads the messages of the server in run,
   which a task of the program runs next to the requests. */
struct client {
    protected implementation info;
    protected client_options settings;
    protected std.async::mutex<u64> counter;
    protected o<stream_link> streams;
    protected o<http_link> http;
};

/* R-SLIB-MCP-0017: a client over the input and output of a server. */
client client::over_streams(own dyn(std.stream::Reader)* input, own dyn(std.stream::Writer)* output,
                            implementation info, client_options settings) throws std.alloc::alloc_error {
    std.bufio::reader<own dyn(std.stream::Reader)*> lines =
        std.bufio::reader<own dyn(std.stream::Reader)*>::create(move input, settings.max_message);
    o<own dyn(std.stream::Writer)*> writer = o::some(move output);
    stream_link link = {.output = std.async::mutex_new(move writer), .input = std.async::mutex_new(move lines),
                        .waiters = std.async::mutex_new(std.array::create::<waiter>())};
    return client {.info = move info, .settings = settings, .counter = std.async::mutex_new(0u64),
                   .streams = o::some(move link), .http = o::none};
}

/* R-SLIB-MCP-0017: a client of the MCP endpoint at an http or https URL; https needs the TLS
   configuration that verifies the server. */
client client::over_http(str endpoint, implementation info, client_options settings, o<arc std.tls::config> tls)
    throws std.alloc::alloc_error {
    std.http::client_options web = {};
    web.bounds.max_body = settings.max_message;
    http_link link = {.endpoint = std.string::from_str(endpoint), .web = web, .tls = move tls,
                      .tools = std.async::mutex_new(std.array::create::<tool>()), .token = o::none};
    return client {.info = move info, .settings = settings, .counter = std.async::mutex_new(0u64),
                   .streams = o::none, .http = o::some(move link)};
}

/* R-SLIB-MCP-0020: the bearer token that a client over HTTP sends in the Authorization field of
   every request. */
void client::set_token(client* this, str token) throws std.alloc::alloc_error {
    o<http_link> taken = core::replace(&this->http, o::none);
    switch (move taken) {
    case variant o::some(move link):
        o<std.string::string> old = core::replace(&link.token, o::some(std.string::from_str(token)));
        drop old;
        o<http_link> empty = core::replace(&this->http, o::some(move link));
        drop empty;
    case variant o::none: break;
    }
}

/* The Authorization field of a request of a link with a token. */
protected void add_token(const http_link* link, std.http::request* post) throws std.alloc::alloc_error {
    switch (link->token) {
    case variant o::some(token):
        std.string::string value = std.string::from_str("Bearer ");
        std.string::append_str(&value, *token);
        try {
            post->headers.add("Authorization", value);
        } catch (std.http::http_error rejected) {
            rejected as void;
        }
    case variant o::none: break;
    }
}

/* The next id of a request of the client. */
@scoped
protected async std.jsonrpc::request_id client::next_id(const client* this) throws std.error::fault {
    std.async::mutex_guard<u64> guard = await this->counter.lock();
    u64* count = std.async::mutex_guard_mut(&guard);
    *count += 1u64;
    return std.jsonrpc::request_id::from_integer(*count as i64);
}

/* The params of a request with _meta: the protocol version, the capabilities and identity of the
   client and, as its progress token, the id of the request. */
protected std.json::value with_meta(std.json::value params, const implementation* info, const client_options* settings,
                                    bool elicit, const std.jsonrpc::request_id* id)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value result = move params;
    std.json::value capabilities = std.json::object();
    if (elicit == true && (settings->form == true || settings->url == true)) {
        std.json::value modes = std.json::object();
        if (settings->form == true) { put(&modes, "form", std.json::object()); }
        if (settings->url == true) { put(&modes, "url", std.json::object()); }
        put(&capabilities, "elicitation", move modes);
    }
    if (settings->tasks == true) {
        std.json::value extensions = std.json::object();
        put(&extensions, tasks_extension, std.json::object());
        put(&capabilities, "extensions", move extensions);
    }
    std.json::value meta = std.json::object();
    put_text(&meta, key_version, protocol_version);
    put(&meta, key_capabilities, move capabilities);
    put(&meta, key_client, to_value(info));
    put(&meta, "progressToken", id->to_json());
    put(&result, "_meta", move meta);
    return move result;
}

/* The id a message of a server is about: the id of a response, the subscription of a
   notification of a listen stream, the token of a progress notification, the request of
   notifications/cancelled. */
protected o<std.jsonrpc::request_id> owner_of(const std.jsonrpc::message* message) throws std.alloc::alloc_error {
    switch (*message) {
    case variant std.jsonrpc::message::result(item): return o::some(item->id.copy());
    case variant std.jsonrpc::message::failure(item):
        switch (item->id) {
        case variant o::some(id): return o::some(id->copy());
        case variant o::none: return o::none;
        }
    case variant std.jsonrpc::message::notification(item):
        switch (item->params) {
        case variant o::some(params):
            switch (member(params, "_meta")) {
            case variant o::some(meta):
                switch (member(*meta, key_subscription)) {
                case variant o::some(id): return std.jsonrpc::request_id::from_json(*id);
                case variant o::none: break;
                }
            case variant o::none: break;
            }
            switch (member(params, "progressToken")) {
            case variant o::some(token): return std.jsonrpc::request_id::from_json(*token);
            case variant o::none: break;
            }
            switch (member(params, "requestId")) {
            case variant o::some(id): return std.jsonrpc::request_id::from_json(*id);
            case variant o::none: break;
            }
        case variant o::none: break;
        }
        return o::none;
    case variant std.jsonrpc::message::request(item):
        item as void;
        return o::none;
    }
    return o::none;
}

/* The sender of the waiter of an id, none when no request of that id is in flight. */
@scoped
protected async o<std.sync::sync_sender<std.jsonrpc::message>> sender_of(const (std.async::mutex<array<waiter>>)* waiters,
                                                                        const std.jsonrpc::request_id* id)
    throws std.error::fault {
    std.async::mutex_guard<array<waiter>> guard = await waiters->lock();
    const array<waiter>* entries = std.async::mutex_guard_ref(&guard);
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if (std.cmp::is_equal(&(*entries)[index].id, id) == true) {
            return o::some(std.sync::clone_sync_sender(&(*entries)[index].sender));
        }
    }
    return o::none;
}

/* Sends a message to a request, waiting while it is behind; false when it is gone. */
@scoped
protected async bool pass_message(const (std.sync::sync_sender<std.jsonrpc::message>)* sender,
                                  std.jsonrpc::message item) throws std.error::fault {
    std.sync::reserve_result<std.jsonrpc::message> room = await std.sync::reserve(sender);
    switch (move room) {
    case variant std.sync::reserve_result::reserved(move permit):
        std.sync::send_permit(move permit, move item);
        return true;
    case variant std.sync::reserve_result::disconnected:
        drop item;
        return false;
    }
    return false;
}

/* Hands a message of the server to the request it is about; a message about no request in
   flight is dropped. */
@scoped
protected async void route(const (std.async::mutex<array<waiter>>)* waiters, std.jsonrpc::message message)
    throws std.error::fault {
    o<std.jsonrpc::request_id> target = owner_of(&message);
    switch (move target) {
    case variant o::some(move id):
        o<std.sync::sync_sender<std.jsonrpc::message>> found = o::none;
        task_scope(1) io {
            o<std.sync::sync_sender<std.jsonrpc::message>> got = await sender_of(waiters, &id);
            o<std.sync::sync_sender<std.jsonrpc::message>> old = core::replace(&found, move got);
            drop old;
        }
        switch (move found) {
        case variant o::some(move sender):
            task_scope(1) io {
                bool sent = await pass_message(&sender, move message);
                sent as void;
            }
        case variant o::none: drop message;
        }
    case variant o::none: drop message;
    }
}

@scoped
protected async void add_waiter(const (std.async::mutex<array<waiter>>)* waiters, waiter entry) throws std.error::fault {
    std.async::mutex_guard<array<waiter>> guard = await waiters->lock();
    append(std.async::mutex_guard_mut(&guard), move entry);
}

@scoped
protected async void remove_waiter(const (std.async::mutex<array<waiter>>)* waiters, const std.jsonrpc::request_id* id)
    throws std.error::fault {
    std.async::mutex_guard<array<waiter>> guard = await waiters->lock();
    array<waiter>* entries = std.async::mutex_guard_mut(&guard);
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if (std.cmp::is_equal(&(*entries)[index].id, id) == true) {
            o<waiter> removed = entries->remove(index);
            drop removed;
            return;
        }
    }
}

/* Ends every request in flight: their messages stop, as when the server ends its output. */
@scoped
protected async void clear_waiters(const (std.async::mutex<array<waiter>>)* waiters) throws std.error::fault {
    std.async::mutex_guard<array<waiter>> guard = await waiters->lock();
    std.array::clear(std.async::mutex_guard_mut(&guard));
}

/* Writes a message on its own line; the lock keeps concurrent messages apart. After close the
   write fails as on a closed stream. */
@scoped
protected async void write_to(const (std.async::mutex<o<own dyn(std.stream::Writer)*>>)* output,
                              const std.jsonrpc::message* message) throws std.json::error, std.error::fault {
    std.async::mutex_guard<o<own dyn(std.stream::Writer)*>> guard = await output->lock();
    switch (*std.async::mutex_guard_ref(&guard)) {
    case variant o::some(writer):
        task_scope(1) io { await std.jsonrpc::write_message(writer, message); }
    case variant o::none:
        throw std.io::io_error {.code = std.io::error_code::closed, .native_code = 0i64};
    }
}

/* R-SLIB-MCP-0019: reads the messages of the server of a stream client and hands each to the
   request it is about until the server ends its output; lines that are no messages are skipped.
   Every request still in flight then ends. A client over HTTP returns at once. */
@scoped
async void client::run(const client* this) throws std.error::fault {
    switch (this->streams) {
    case variant o::some(link):
        std.async::mutex_guard<std.bufio::reader<own dyn(std.stream::Reader)*>> guard = await link->input.lock();
        bool reading = true;
        while (reading == true) {
            o<bytes> next = o::none;
            task_scope(1) io {
                o<bytes> got = await std.jsonrpc::read_line(std.async::mutex_guard_mut(&guard));
                o<bytes> old = core::replace(&next, move got);
                drop old;
            }
            switch (move next) {
            case variant o::some(move line):
                try {
                    std.jsonrpc::message parsed = std.jsonrpc::parse(line.as_slice());
                    task_scope(1) io { await route(&link->waiters, move parsed); }
                } catch (std.jsonrpc::rpc_error rejected) {
                    (move rejected) as void;
                }
            case variant o::none: reading = false;
            }
        }
        drop guard;
        task_scope(1) io { await clear_waiters(&link->waiters); }
    case variant o::none: break;
    }
}

/* R-SLIB-MCP-0019: ends the input of the server of a stream client, which a server over stdio
   takes as the signal to finish; run returns when the server has ended its output. */
@scoped
async void client::close(const client* this) throws std.error::fault {
    switch (this->streams) {
    case variant o::some(link):
        std.async::mutex_guard<o<own dyn(std.stream::Writer)*>> guard = await link->output.lock();
        o<own dyn(std.stream::Writer)*> taken = core::replace(std.async::mutex_guard_mut(&guard), o::none);
        drop guard;
        switch (move taken) {
        case variant o::some(move writer):
            task_scope(1) io { await writer->shutdown(); }
            drop writer;
        case variant o::none: break;
        }
    case variant o::none: break;
    }
}

protected mcp_error closed_error() throws std.alloc::alloc_error {
    return failure(connection_closed, "the connection to the server ended");
}

protected mcp_error broken(str message) throws std.alloc::alloc_error {
    return failure(protocol_violation, message);
}

/* Tells the server over a stream that the client no longer waits for a request. */
@scoped
protected async void cancel_on(const stream_link* link, const std.jsonrpc::request_id* id, str reason)
    throws std.error::fault {
    try {
        std.json::value params = std.json::object();
        put(&params, "requestId", id->to_json());
        put_text(&params, "reason", reason);
        std.jsonrpc::message note = std.jsonrpc::message::notification(std.jsonrpc::notification {
            .method = std.string::from_str("notifications/cancelled"), .params = o::some(move params)});
        task_scope(1) io { await write_to(&link->output, &note); }
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
}

/* One request over a stream: its response, the messages about it before the response being
   dropped. A request without response before the timeout is cancelled. */
@scoped
protected async std.jsonrpc::message stream_call(const stream_link* link, std.jsonrpc::request message,
                                                 std.time::duration timeout)
    throws mcp_error, std.error::fault {
    std.jsonrpc::request_id id = message.id.copy();
    std.sync::sync_channel<std.jsonrpc::message> factory = std.sync::sync_channel::<std.jsonrpc::message>(16usize);
    std.sync::sync_sender<std.jsonrpc::message> sender = std.sync::sync_sender(&factory);
    std.sync::receiver<std.jsonrpc::message> inbox = std.sync::sync_receiver(move factory);
    waiter entry = {.id = id.copy(), .sender = move sender};
    task_scope(1) io { await add_waiter(&link->waiters, move entry); }
    std.jsonrpc::message request_message = std.jsonrpc::message::request(move message);
    bool written = true;
    try {
        task_scope(1) io { await write_to(&link->output, &request_message); }
    } catch (std.json::error rejected) {
        (move rejected) as void;
        written = false;
    }
    drop request_message;
    if (written == false) {
        drop inbox;
        task_scope(1) io { await remove_waiter(&link->waiters, &id); }
        throw broken("the request could not be written");
    }
    std.time::instant now = std.time::monotonic_now();
    std.time::instant limit = std.time::instant_add(now, timeout);
    o<std.jsonrpc::message> reply = o::none;
    bool waiting = true;
    bool expired = false;
    while (waiting == true) {
        task_scope(1) wait {
            auto next = inbox.receive();
            select (wait) {
            case o<std.jsonrpc::message> got = await move next:
                switch (move got) {
                case variant o::some(move item):
                    if (is_response(&item) == true) {
                        o<std.jsonrpc::message> old = core::replace(&reply, o::some(move item));
                        drop old;
                        waiting = false;
                    } else {
                        drop item;
                    }
                case variant o::none: waiting = false;
                }
            case until (limit):
                expired = true;
                waiting = false;
            }
            wait.cancel_all();
        }
    }
    limit as void;
    drop inbox;
    task_scope(1) io { await remove_waiter(&link->waiters, &id); }
    if (expired == true) {
        task_scope(1) io { await cancel_on(link, &id, "timeout"); }
        throw failure(request_timeout, "the request took longer than its timeout");
    }
    switch (move reply) {
    case variant o::some(move item): return move item;
    case variant o::none: throw closed_error();
    }
    throw closed_error();
}

/* The text of a header value: as it is when it has visible ASCII and inner spaces only and does
   not look like the Base64 sentinel, otherwise =?base64?...?=. */
protected std.string::string header_text(str value) throws std.alloc::alloc_error {
    const u8[] bytes = value;
    bool plain = true;
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        if (bytes[index] < 32u8 || bytes[index] > 126u8) { plain = false; }
    }
    if (len(bytes) != 0usize && (bytes[0usize] == 32u8 || bytes[len(bytes) - 1usize] == 32u8)) { plain = false; }
    if (std.text::starts_with(value, "=?base64?") == true && std.text::ends_with(value, "?=") == true) { plain = false; }
    if (plain == true) { return std.string::from_str(value); }
    std.string::string text = std.string::from_str("=?base64?");
    std.string::string encoded = std.encoding::encode_base64(bytes);
    std.string::append_str(&text, encoded);
    std.string::append_str(&text, "?=");
    return move text;
}

/* The Mcp-Param headers of a call of a tool: each mirrored argument that is present and not
   null, as text. */
protected void mirror_headers(const tool* definition, const std.json::value* arguments, std.http::headers* fields)
    throws std.alloc::alloc_error {
    array<mirror> found = mirrors_of(&definition->input_schema);
    for (usize index = 0usize; index < len(found); index += 1usize) {
        switch (argument_at(arguments, &found[index].path)) {
        case variant o::some(value):
            std.json::value_kind kind = std.json::kind(*value);
            o<std.string::string> text = o::none;
            if (kind == std.json::value_kind::string) { text = o::some(header_text(std.json::text(*value))); }
            if (kind == std.json::value_kind::number) { text = o::some(std.string::from_str(std.json::text(*value))); }
            if (kind == std.json::value_kind::boolean) {
                if (std.json::boolean(*value) == true) {
                    text = o::some(std.string::from_str("true"));
                } else {
                    text = o::some(std.string::from_str("false"));
                }
            }
            switch (move text) {
            case variant o::some(move spelled):
                std.string::string name = std.string::from_str("Mcp-Param-");
                std.string::append_str(&name, found[index].header);
                try {
                    fields->add(name, spelled);
                } catch (std.http::http_error rejected) {
                    rejected as void;
                }
            case variant o::none: break;
            }
        case variant o::none: break;
        }
    }
}

/* The Mcp-Param headers of a call of a tool that the server listed. */
@scoped
protected async void add_mirrors(const http_link* link, str name, const std.json::value* params, std.http::headers* fields)
    throws std.error::fault {
    std.async::mutex_guard<array<tool>> guard = await link->tools.lock();
    const array<tool>* known = std.async::mutex_guard_ref(&guard);
    std.json::value empty = std.json::null();
    const std.json::value* arguments = &empty;
    switch (member(params, "arguments")) {
    case variant o::some(given): arguments = *given;
    case variant o::none: break;
    }
    for (usize index = 0usize; index < len(*known); index += 1usize) {
        if (std.bytes::equal((*known)[index].name, name) == true) {
            mirror_headers(&(*known)[index], arguments, fields);
        }
    }
}

/* The message of a response body in JSON, none when the body is no message. */
@scoped
protected async o<std.jsonrpc::message> json_message(std.http::streamed* opened, usize limit)
    throws std.http::http_error, std.error::fault {
    bytes whole = {};
    bool more = true;
    while (more == true) {
        o<bytes> arrived = o::none;
        task_scope(1) io {
            o<bytes> got = await opened->next();
            o<bytes> old = core::replace(&arrived, move got);
            drop old;
        }
        switch (move arrived) {
        case variant o::some(move data):
            throw (len(whole) + len(data) > limit) std.http::http_error {.code = std.http::error_code::body_too_large};
            std.bytes::append(&whole, data.as_slice());
        case variant o::none: more = false;
        }
    }
    if (len(whole) == 0usize) { return o::none; }
    try {
        return o::some(std.jsonrpc::parse(whole.as_slice()));
    } catch (std.jsonrpc::rpc_error rejected) {
        (move rejected) as void;
    }
    return o::none;
}

/* The first response among the events of an event stream, none when the stream ends without
   one; the other messages, such as progress, are dropped. */
@scoped
protected async o<std.jsonrpc::message> event_message(std.http::streamed* opened) throws std.http::http_error, std.error::fault {
    std.http::sse_parser parser = std.http::sse_parser::create();
    bool more = true;
    o<std.jsonrpc::message> found_reply = o::none;
    while (more == true) {
        o<bytes> arrived = o::none;
        task_scope(1) io {
            o<bytes> got = await opened->next();
            o<bytes> old = core::replace(&arrived, move got);
            drop old;
        }
        switch (move arrived) {
        case variant o::some(move data):
            parser.feed(data.as_slice());
            bool scanning = true;
            while (scanning == true) {
                o<std.http::sse_message> event = parser.next();
                switch (move event) {
                case variant o::some(move found):
                    try {
                        std.jsonrpc::message parsed = std.jsonrpc::parse(found.data);
                        if (is_response(&parsed) == true) {
                            o<std.jsonrpc::message> old = core::replace(&found_reply, o::some(move parsed));
                            drop old;
                            scanning = false;
                            more = false;
                        } else {
                            drop parsed;
                        }
                    } catch (std.jsonrpc::rpc_error rejected) {
                        (move rejected) as void;
                    }
                case variant o::none: scanning = false;
                }
            }
        case variant o::none: more = false;
        }
    }
    drop parser;
    return move found_reply;
}

/* The message of a response: its JSON body, or the first response of its event stream. */
@scoped
protected async o<std.jsonrpc::message> response_message(std.http::streamed* opened, usize limit)
    throws std.http::http_error, std.error::fault {
    bool events = false;
    switch (opened->head.headers.get("Content-Type")) {
    case variant o::some(kind): events = media_is(*kind, "text/event-stream");
    case variant o::none: break;
    }
    if (events == true) {
        task_scope(1) io { return await event_message(opened); }
    }
    task_scope(1) io { return await json_message(opened, limit); }
    return o::none;
}

/* An HTTP client of the settings of a link. */
protected std.http::client web_client(const http_link* link) {
    switch (link->tls) {
    case variant o::some(settings): return std.http::client::with_tls(link->web, std.arc::clone(settings));
    case variant o::none: break;
    }
    return std.http::client::create(link->web);
}

/* Posts a request and returns the message of the response; status receives the status. */
@scoped
protected async o<std.jsonrpc::message> post_message(const std.http::client* web, std.http::request post, str endpoint,
                                                     usize limit, u16* status)
    throws std.http::http_error, std.tls::tls_error, std.error::fault {
    o<std.http::streamed> opened = o::none;
    task_scope(1) io {
        std.http::streamed got = await web->open(move post, endpoint);
        o<std.http::streamed> old = core::replace(&opened, o::some(move got));
        drop old;
    }
    switch (move opened) {
    case variant o::some(move stream):
        *status = stream.head.status;
        task_scope(1) io { return await response_message(&stream, limit); }
    case variant o::none: break;
    }
    return o::none;
}

/* One request over HTTP: a POST with the headers of MCP and the response of its body, within
   the timeout. */
@scoped
protected async std.jsonrpc::message http_call(const http_link* link, std.jsonrpc::request message, str name,
                                               std.http::headers extra, std.time::duration timeout, usize limit)
    throws mcp_error, std.error::fault {
    std.jsonrpc::message request_message = std.jsonrpc::message::request(move message);
    o<std.string::string> encoded = encoded_text(&request_message);
    std.string::string method = std.string::create();
    switch (request_message) {
    case variant std.jsonrpc::message::request(item): std.string::append_str(&method, item->method);
    default: break;
    }
    drop request_message;
    std.string::string text = text_or_empty(move encoded);
    throw (std.string::len(&text) == 0usize) broken("the request could not be written");
    std.http::request post = std.http::request::create(std.http::method::post, "/");
    try {
        post.headers.add("Content-Type", "application/json");
        post.headers.add("Accept", "application/json, text/event-stream");
        post.headers.add("MCP-Protocol-Version", protocol_version);
        post.headers.add("Mcp-Method", method);
        const u8[] named = name;
        if (len(named) != 0usize) {
            std.string::string value = header_text(name);
            post.headers.add("Mcp-Name", value);
        }
        for (usize index = 0usize; index < extra.count(); index += 1usize) {
            post.headers.add(extra.name_at(index), extra.value_at(index));
        }
    } catch (std.http::http_error rejected) {
        rejected as void;
        throw broken("a header of the request is not valid");
    }
    std.bytes::append(&post.body, text);
    add_token(link, &post);
    std.http::client web = web_client(link);
    std.time::instant now = std.time::monotonic_now();
    std.time::instant limit_time = std.time::instant_add(now, timeout);
    o<std.jsonrpc::message> reply = o::none;
    u16 status = 0u16;
    try {
        deadline (limit_time) {
            task_scope(1) io {
                o<std.jsonrpc::message> got = await post_message(&web, move post, link->endpoint, limit,
                                                                 &status);
                o<std.jsonrpc::message> old = core::replace(&reply, move got);
                drop old;
            }
        }
    } catch (std.http::http_error rejected) {
        rejected as void;
        throw closed_error();
    } catch (std.tls::tls_error rejected) {
        rejected as void;
        throw closed_error();
    } catch (std.error::fault rejected) {
        rejected as void;
        throw failure(request_timeout, "the request failed or took longer than its timeout");
    }
    switch (move reply) {
    case variant o::some(move item): return move item;
    case variant o::none: break;
    }
    throw (status == 401u16) failure(unauthorized, "the server requires a valid bearer token");
    throw (status == 403u16) failure(forbidden, "the token of the client lacks a scope that the request needs");
    std.string::string problem = f"the server answered with HTTP status {status} and no message";
    throw failure(protocol_violation, problem);
}

/* The value of the Mcp-Name header of a request: the name of a tool or prompt, the URI of a
   resource, the id of a task (R-SLIB-MCP-0022), empty for other methods. */
protected std.string::string target_name(str method, const std.json::value* params) throws std.alloc::alloc_error {
    if (std.bytes::equal(method, "tools/call") == true || std.bytes::equal(method, "prompts/get") == true) {
        return owned(member_text(params, "name"));
    }
    if (std.bytes::equal(method, "resources/read") == true) { return owned(member_text(params, "uri")); }
    if (is_task_method(method) == true) { return owned(member_text(params, "taskId")); }
    return std.string::create();
}

/* The result of a response, or its error as mcp_error. */
protected std.json::value result_of(std.jsonrpc::message reply) throws mcp_error, std.alloc::alloc_error {
    switch (move reply) {
    case variant std.jsonrpc::message::result(move item):
        return match (move item) { case { .result = move value }: move value; };
    case variant std.jsonrpc::message::failure(move item):
        throw mcp_error {.code = item.code, .message = std.string::from_str(item.message)};
    default: break;
    }
    throw broken("the server answered with no response");
}

/* One request over HTTP with its Mcp-Name and Mcp-Param headers. */
@scoped
protected async std.json::value http_request(const client* owner, std.jsonrpc::request_id id, str method, std.json::value body)
    throws mcp_error, std.error::fault {
    std.string::string name = target_name(method, &body);
    std.http::headers extra = std.http::headers::create();
    switch (owner->http) {
    case variant o::some(link):
        if (std.bytes::equal(method, "tools/call") == true) {
            task_scope(1) io { await add_mirrors(link, name, &body, &extra); }
        }
        std.jsonrpc::request message = {.id = move id, .method = std.string::from_str(method),
                                        .params = o::some(move body)};
        task_scope(1) io {
            std.jsonrpc::message reply = await http_call(link, move message, name, move extra,
                                                         owner->settings.timeout, owner->settings.max_message);
            return result_of(move reply);
        }
    case variant o::none:
        drop id;
        drop body;
        drop extra;
        drop name;
    }
    throw closed_error();
}

/* R-SLIB-MCP-0017: sends one request with the _meta of the client and returns the result of
   its response; an error response is thrown as mcp_error with its code and message. The client
   declares its elicitation modes only when elicit is true. */
@scoped
protected async std.json::value client::request(const client* this, str method, std.json::value params, bool elicit)
    throws mcp_error, std.error::fault {
    std.jsonrpc::request_id id = std.jsonrpc::request_id::from_integer(0i64);
    task_scope(1) io {
        std.jsonrpc::request_id next = await this->next_id();
        std.jsonrpc::request_id old = core::replace(&id, move next);
        drop old;
    }
    o<std.json::value> prepared = o::none;
    try {
        std.json::value full = with_meta(move params, &this->info, &this->settings, elicit, &id);
        o<std.json::value> old = core::replace(&prepared, o::some(move full));
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    std.json::value body = value_or_null(move prepared);
    throw (std.json::kind(&body) != std.json::value_kind::object) broken("the request could not be written");
    switch (this->streams) {
    case variant o::some(link):
        std.jsonrpc::request message = {.id = move id, .method = std.string::from_str(method),
                                        .params = o::some(move body)};
        task_scope(1) io {
            std.jsonrpc::message reply = await stream_call(link, move message, this->settings.timeout);
            return result_of(move reply);
        }
    case variant o::none:
        task_scope(1) io { return await http_request(this, move id, method, move body); }
    }
    throw closed_error();
}

/* The kind of a result: 0 complete, also when it names none (a server of an earlier revision), 1
   input_required and 2 a task (R-SLIB-MCP-0024). */
protected u8 result_kind(const std.json::value* result) throws mcp_error, std.alloc::alloc_error {
    switch (member_text(result, "resultType")) {
    case variant o::some(kind):
        if (std.bytes::equal(*kind, "complete") == true) { return 0u8; }
        if (std.bytes::equal(*kind, "input_required") == true) { return 1u8; }
        throw (std.bytes::equal(*kind, "task") == false) broken("unknown resultType");
        return 2u8;
    case variant o::none: return 0u8;
    }
}

/* R-SLIB-MCP-0017: how a client answers the input requests of a server: a handler with shared
   state S that asks the user and returns the answer. */
@generic<S: send & sync & unborrowed>
struct elicitor {
    arc S state;
    async fn(arc S, elicitation) -> answer throws(std.error::fault) handler;
};

/* The input request under a key of inputRequests: an elicitation of a mode that the client
   declared. */
protected elicitation elicitation_of(const std.json::value* request, const client_options* settings)
    throws mcp_error, std.json::error, std.alloc::alloc_error {
    switch (member_text(request, "method")) {
    case variant o::some(method):
        throw (std.bytes::equal(*method, "elicitation/create") == false)
            broken("the server asked for input the client does not give");
    case variant o::none: throw broken("an input request without method");
    }
    switch (member(request, "params")) {
    case variant o::some(params):
        str mode = "form";
        switch (member_text(*params, "mode")) {
        case variant o::some(given): mode = *given;
        case variant o::none: break;
        }
        std.string::string message = owned(member_text(*params, "message"));
        if (std.bytes::equal(mode, "url") == true) {
            throw (settings->url == false) broken("the server asked for a URL the client did not allow");
            switch (member_text(*params, "url")) {
            case variant o::some(url):
                return elicitation::url(url_request {.message = move message, .url = std.string::from_str(*url)});
            case variant o::none: throw broken("a URL request without url");
            }
        }
        throw (std.bytes::equal(mode, "form") == false || settings->form == false)
            broken("the server asked for a form the client did not allow");
        switch (member(*params, "requestedSchema")) {
        case variant o::some(schema):
            return elicitation::form(form_request {.message = move message, .schema = copy_value(*schema)});
        case variant o::none: throw broken("a form without requestedSchema");
        }
    case variant o::none: break;
    }
    throw broken("an input request without params");
}

protected std.json::value answer_value(const answer* given) throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    switch (given->action) {
    case answer_action::accept: put_text(&result, "action", "accept");
    case answer_action::decline: put_text(&result, "action", "decline");
    case answer_action::cancel: put_text(&result, "action", "cancel");
    }
    switch (given->content) {
    case variant o::some(fields): put(&result, "content", copy_value(fields));
    case variant o::none: break;
    }
    return move result;
}

/* The params of the next round: those of the request with the answers and the echoed state. */
protected std.json::value next_round(const std.json::value* params, std.json::value answers, o<std.string::string> state)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    for (usize index = 0usize; index < std.json::len(params); index += 1usize) {
        str key = std.json::key_at(params, index);
        if (std.bytes::equal(key, "inputResponses") == true || std.bytes::equal(key, "requestState") == true) {
            continue;
        }
        switch (std.json::find(params, key)) {
        case variant o::some(value): put(&result, key, copy_value(*value));
        case variant o::none: break;
        }
    }
    if (std.json::len(&answers) != 0usize) {
        put(&result, "inputResponses", move answers);
    } else {
        drop answers;
    }
    switch (move state) {
    case variant o::some(move text): put_text(&result, "requestState", text);
    case variant o::none: break;
    }
    return move result;
}

/* A request that may need input: the rounds of a Multi Round-Trip Request, each answering the
   input requests of the previous one through the elicitor, up to the rounds of the settings. A
   task that the server made is returned as it came. */
@generic<S: send & sync & unborrowed>
@scoped
protected async std.json::value client::round_trips(const client* this, str method, std.json::value params,
                                                    const elicitor<S>* answers, bool elicit)
    throws mcp_error, std.error::fault {
    std.json::value current = move params;
    for (u32 round = 0u32; round < this->settings.rounds; round += 1u32) {
        std.json::value sent = std.json::null();
        try {
            std.json::value copied = copy_value(&current);
            std.json::value old = core::replace(&sent, move copied);
            drop old;
        } catch (std.json::error rejected) {
            (move rejected) as void;
            throw broken("the request could not be written");
        }
        std.json::value result = std.json::null();
        task_scope(1) io {
            std.json::value got = await this->request(method, move sent, elicit);
            std.json::value old = core::replace(&result, move got);
            drop old;
        }
        if (result_kind(&result) != 1u8) { return move result; }
        std.json::value responses = empty_object();
        switch (member(&result, "inputRequests")) {
        case variant o::some(requests):
            throw (std.json::kind(*requests) != std.json::value_kind::object) broken("inputRequests is no object");
            for (usize index = 0usize; index < std.json::len(*requests); index += 1usize) {
                str key = std.json::key_at(*requests, index);
                o<elicitation> asked = o::none;
                switch (std.json::find(*requests, key)) {
                case variant o::some(request):
                    try {
                        asked = o::some(elicitation_of(*request, &this->settings));
                    } catch (std.json::error rejected) {
                        (move rejected) as void;
                    }
                case variant o::none: break;
                }
                switch (move asked) {
                case variant o::some(move request):
                    auto handler = answers->handler;
                    answer given = await handler(std.arc::clone(&answers->state), move request);
                    try {
                        put(&responses, key, answer_value(&given));
                    } catch (std.json::error rejected) {
                        (move rejected) as void;
                    }
                case variant o::none: throw broken("an input request of no known kind");
                }
            }
        case variant o::none: break;
        }
        o<std.string::string> state = o::none;
        switch (member_text(&result, "requestState")) {
        case variant o::some(text): state = o::some(std.string::from_str(*text));
        case variant o::none: break;
        }
        try {
            std.json::value next = next_round(&current, move responses, move state);
            std.json::value old = core::replace(&current, move next);
            drop old;
        } catch (std.json::error rejected) {
            (move rejected) as void;
            throw broken("the next round could not be written");
        }
        drop result;
    }
    throw broken("the server asked for input more often than the rounds allow");
}

/* ---- Tasks of a client (R-SLIB-MCP-0024) ---- */

/* The status of a task in its JSON; none for a status of no known name. */
protected o<task_status> status_in(const std.json::value* state) {
    switch (member_text(state, "status")) {
    case variant o::some(text):
        if (std.bytes::equal(*text, "working") == true) { return o::some(task_status::working); }
        if (std.bytes::equal(*text, "input_required") == true) { return o::some(task_status::input_required); }
        if (std.bytes::equal(*text, "completed") == true) { return o::some(task_status::completed); }
        if (std.bytes::equal(*text, "failed") == true) { return o::some(task_status::failed); }
        if (std.bytes::equal(*text, "cancelled") == true) { return o::some(task_status::cancelled); }
    case variant o::none: break;
    }
    return o::none;
}

protected task_status known_status(const std.json::value* state) throws mcp_error, std.alloc::alloc_error {
    switch (status_in(state)) {
    case variant o::some(found): return *found;
    case variant o::none: break;
    }
    throw broken("a task of no known status");
}

/* The error of a failed task as mcp_error with its code and message. */
protected mcp_error task_error(const std.json::value* state) throws std.alloc::alloc_error {
    switch (member(state, "error")) {
    case variant o::some(problem):
        i64 code = std.jsonrpc::internal_error;
        switch (member(*problem, "code")) {
        case variant o::some(number):
            if (std.json::kind(*number) == std.json::value_kind::number) {
                try {
                    code = std.convert::parse_i64(std.json::text(*number), 10u32);
                } catch (std.convert::parse_error rejected) {
                    rejected as void;
                }
            }
        case variant o::none: break;
        }
        return mcp_error {.code = code, .message = owned(member_text(*problem, "message"))};
    case variant o::none: break;
    }
    return failure(protocol_violation, "a failed task without its error");
}

/* The interval at which a client polls a task: pollIntervalMs held between 10 milliseconds and a
   minute, a second without it. */
protected std.time::duration poll_interval(const std.json::value* state) {
    u64 count = 1000u64;
    switch (unsigned_member(state, "pollIntervalMs")) {
    case variant o::some(given): count = *given;
    case variant o::none: break;
    }
    if (count < 10u64) { count = 10u64; }
    if (count > 60000u64) { count = 60000u64; }
    return millis(count);
}

/* The params of a request about a task. */
protected std.json::value task_params(str id) throws std.alloc::alloc_error {
    try {
        std.json::value params = std.json::object();
        put_text(&params, "taskId", id);
        return move params;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.json::null();
}

/* Whether a key is among those a client answered. */
protected bool answered_key(const array<std.string::string>* answered, str key) {
    for (usize index = 0usize; index < len(*answered); index += 1usize) {
        if (std.bytes::equal((*answered)[index], key) == true) { return true; }
    }
    return false;
}

/* The answers of the elicitor to the input requests of a task that the client has not answered
   yet; their keys join those answered. */
@generic<S: send & sync & unborrowed>
@scoped
protected async std.json::value client::task_answers(const client* this, const std.json::value* state,
                                                     const elicitor<S>* answers, array<std.string::string>* answered)
    throws mcp_error, std.error::fault {
    std.json::value responses = empty_object();
    switch (member(state, "inputRequests")) {
    case variant o::some(requests):
        throw (std.json::kind(*requests) != std.json::value_kind::object) broken("inputRequests is no object");
        for (usize index = 0usize; index < std.json::len(*requests); index += 1usize) {
            str key = std.json::key_at(*requests, index);
            if (answered_key(answered, key) == true) { continue; }
            o<elicitation> asked = o::none;
            switch (std.json::find(*requests, key)) {
            case variant o::some(request):
                try {
                    asked = o::some(elicitation_of(*request, &this->settings));
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                }
            case variant o::none: break;
            }
            switch (move asked) {
            case variant o::some(move request):
                auto handler = answers->handler;
                answer given = await handler(std.arc::clone(&answers->state), move request);
                try {
                    put(&responses, key, answer_value(&given));
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                }
                add_text(answered, key);
            case variant o::none: throw broken("an input request of no known kind");
            }
        }
    case variant o::none: break;
    }
    return move responses;
}

/* The end of a task: the result of a completed one; a failed one throws its error and a
   cancelled one task_cancelled. */
protected std.json::value task_end(std.json::value state, task_status status) throws mcp_error, std.alloc::alloc_error {
    throw (status == task_status::failed) task_error(&state);
    throw (status == task_status::cancelled) failure(task_cancelled, "the task was cancelled");
    try {
        return std.json::take_field(&state, "result");
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw broken("a completed task without its result");
}

/* R-SLIB-MCP-0024: follows a task to its end: polls tasks/get at the interval that the server
   suggests, answers each input request once through the elicitor with tasks/update, and returns
   the result of the task; a failed task throws its error and a cancelled one task_cancelled. */
@generic<S: send & sync & unborrowed>
@scoped
protected async std.json::value client::follow_task(const client* this, std.json::value created, const elicitor<S>* answers)
    throws mcp_error, std.error::fault {
    std.string::string id = owned(member_text(&created, "taskId"));
    throw (std.string::len(&id) == 0usize) broken("a task without taskId");
    std.json::value state = move created;
    array<std.string::string> answered = std.array::create::<std.string::string>();
    while (true) {
        task_status status = known_status(&state);
        if (is_final(status) == true) {
            drop answered;
            return task_end(move state, status);
        }
        if (status == task_status::input_required) {
            std.json::value responses = empty_object();
            task_scope(1) io {
                std.json::value found = await this->task_answers(&state, answers, &answered);
                std.json::value old = core::replace(&responses, move found);
                drop old;
            }
            if (std.json::len(&responses) != 0usize) {
                std.json::value params = task_params(id);
                try {
                    put(&params, "inputResponses", move responses);
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                    throw broken("the answers could not be written");
                }
                task_scope(1) io {
                    std.json::value receipt = await this->request("tasks/update", move params, false);
                    drop receipt;
                }
            } else {
                drop responses;
            }
        }
        await std.time::sleep_for(poll_interval(&state));
        task_scope(1) io {
            std.json::value got = await this->request("tasks/get", task_params(id), false);
            std.json::value old = core::replace(&state, move got);
            drop old;
        }
    }
    throw broken("the task did not end");
}

/* The state and handler of a request that gives no input. */
protected struct nobody { u8 unused; };

protected async answer refuse_input(arc nobody state, elicitation request) throws std.error::fault {
    drop state;
    drop request;
    return answer {.action = answer_action::cancel, .content = o::none};
}

protected elicitor<nobody> no_input() throws std.alloc::alloc_error {
    return elicitor<nobody> {.state = new arc nobody {.unused = 0u8}, .handler = refuse_input};
}

/* R-SLIB-MCP-0017: what server/discover answers. */
struct discovery {
    array<std.string::string> versions;
    std.json::value capabilities;
    std.string::string instructions;
    implementation server;
    u64 ttl_ms;
    bool private_cache;
};

/* R-SLIB-MCP-0017: server/discover. A server whose versions do not include 2026-07-28 is not
   supported: the result names the versions, and requests to it fail. */
@scoped
async discovery client::discover(const client* this) throws mcp_error, std.error::fault {
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->request("server/discover", empty_object(), false);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    try {
        array<std.string::string> versions = std.array::create::<std.string::string>();
        switch (member(&result, "supportedVersions")) {
        case variant o::some(listed):
            for (usize index = 0usize; index < std.json::len(*listed); index += 1usize) {
                switch (std.json::get(*listed, index)) {
                case variant o::some(item): add_text(&versions, std.json::text(*item));
                case variant o::none: break;
                }
            }
        case variant o::none: break;
        }
        implementation server = implementation::create("", "");
        switch (member(&result, "_meta")) {
        case variant o::some(meta):
            switch (member(*meta, key_server)) {
            case variant o::some(info):
                std.string::string text = std.json::stringify(*info);
                implementation found = std.json::unmarshal(text);
                implementation old = core::replace(&server, move found);
                drop old;
            case variant o::none: break;
            }
        case variant o::none: break;
        }
        bool private_cache = false;
        switch (member_text(&result, "cacheScope")) {
        case variant o::some(scope): private_cache = std.bytes::equal(*scope, "private");
        case variant o::none: break;
        }
        std.json::value capabilities = std.json::object();
        switch (member(&result, "capabilities")) {
        case variant o::some(found):
            std.json::value copied = copy_value(*found);
            std.json::value old = core::replace(&capabilities, move copied);
            drop old;
        case variant o::none: break;
        }
        o<u64> ttl = unsigned_member(&result, "ttlMs");
        u64 ttl_ms = 0u64;
        switch (ttl) {
        case variant o::some(value): ttl_ms = *value;
        case variant o::none: break;
        }
        return discovery {.versions = move versions, .capabilities = move capabilities,
                          .instructions = owned(member_text(&result, "instructions")), .server = move server,
                          .ttl_ms = ttl_ms, .private_cache = private_cache};
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw broken("server/discover returned no discovery");
}

protected bool present_text(const (o<std.string::string>)* value) {
    switch (*value) {
    case variant o::some(text):
        text as void;
        return true;
    case variant o::none: return false;
    }
}

/* Every entry of a paginated list: the pages of the method, following nextCursor. */
@scoped
protected async array<std.json::value> client::list_all(const client* this, str method, str key)
    throws mcp_error, std.error::fault {
    array<std.json::value> entries = std.array::create::<std.json::value>();
    o<std.string::string> cursor = o::none;
    for (u32 page = 0u32; page < 1000u32; page += 1u32) {
        std.json::value params = empty_object();
        switch (cursor) {
        case variant o::some(text):
            try {
                put_text(&params, "cursor", *text);
            } catch (std.json::error rejected) {
                (move rejected) as void;
            }
        case variant o::none: break;
        }
        std.json::value result = std.json::null();
        task_scope(1) io {
            std.json::value got = await this->request(method, move params, false);
            std.json::value old = core::replace(&result, move got);
            drop old;
        }
        try {
            switch (member(&result, key)) {
            case variant o::some(listed):
                for (usize index = 0usize; index < std.json::len(*listed); index += 1usize) {
                    switch (std.json::get(*listed, index)) {
                    case variant o::some(item): append(&entries, copy_value(*item));
                    case variant o::none: break;
                    }
                }
            case variant o::none: break;
            }
        } catch (std.json::error rejected) {
            (move rejected) as void;
        }
        o<std.string::string> next = o::none;
        switch (member_text(&result, "nextCursor")) {
        case variant o::some(text): next = o::some(std.string::from_str(*text));
        case variant o::none: break;
        }
        o<std.string::string> old = core::replace(&cursor, move next);
        drop old;
        if (present_text(&cursor) == false) { return move entries; }
    }
    throw broken("a list with more than 1000 pages");
}


/* Whether a header name is a token of RFC 9110. */
protected bool is_token(str name) {
    const u8[] bytes = name;
    if (len(bytes) == 0usize) { return false; }
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 value = bytes[index];
        bool letter = (value >= 65u8 && value <= 90u8) || (value >= 97u8 && value <= 122u8);
        bool digit = value >= 48u8 && value <= 57u8;
        bool mark = value == 33u8 || value == 35u8 || value == 36u8 || value == 37u8 || value == 38u8 ||
                    value == 39u8 || value == 42u8 || value == 43u8 || value == 45u8 || value == 46u8 ||
                    value == 94u8 || value == 95u8 || value == 96u8 || value == 124u8 || value == 126u8;
        if (letter == false && digit == false && mark == false) { return false; }
    }
    return true;
}

/* Whether the type of a mirrored property is string, integer or boolean, alone or with null. */
protected bool mirrorable(const std.json::value* property) {
    switch (member(property, "type")) {
    case variant o::some(kind):
        if (std.json::kind(*kind) == std.json::value_kind::string) {
            str name = std.json::text(*kind);
            return std.bytes::equal(name, "string") == true || std.bytes::equal(name, "integer") == true ||
                   std.bytes::equal(name, "boolean") == true;
        }
        if (std.json::kind(*kind) != std.json::value_kind::array || std.json::len(*kind) != 2usize) { return false; }
        u32 primitive = 0u32;
        u32 nulls = 0u32;
        for (usize index = 0usize; index < 2usize; index += 1usize) {
            switch (std.json::get(*kind, index)) {
            case variant o::some(item):
                str name = std.json::text(*item);
                if (std.bytes::equal(name, "null") == true) { nulls += 1u32; }
                if (std.bytes::equal(name, "string") == true || std.bytes::equal(name, "integer") == true ||
                    std.bytes::equal(name, "boolean") == true) {
                    primitive += 1u32;
                }
            case variant o::none: break;
            }
        }
        return primitive == 1u32 && nulls == 1u32;
    case variant o::none: return false;
    }
}

/* R-SLIB-MCP-0017: whether a client over HTTP may use a tool: its input schema describes an
   object, and each x-mcp-header names a distinct token and annotates a string, integer or
   Boolean property that properties alone reach. */
protected bool usable_tool(const tool* definition) throws std.alloc::alloc_error {
    if (object_schema(&definition->input_schema) == false) { return false; }
    array<mirror> found = mirrors_of(&definition->input_schema);
    for (usize index = 0usize; index < len(found); index += 1usize) {
        if (is_token(found[index].header) == false) { return false; }
        for (usize other = 0usize; other < index; other += 1usize) {
            if (std.text::equal_ignore_ascii_case(found[other].header, found[index].header) == true) {
                return false;
            }
        }
        switch (property_at(&definition->input_schema, &found[index].path)) {
        case variant o::some(property):
            if (mirrorable(*property) == false) { return false; }
        case variant o::none: return false;
        }
    }
    try {
        std.string::string text = std.json::stringify(&definition->input_schema);
        usize annotations = 0usize;
        const u8[] bytes = text;
        str needle = "\"x-mcp-header\":";
        const u8[] pattern = needle;
        usize at = 0usize;
        while (at + len(pattern) <= len(bytes)) {
            if (std.bytes::equal(bytes[at..at + len(pattern)], pattern) == true) { annotations += 1usize; }
            at += 1usize;
        }
        return annotations == len(found);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return false;
}

@generic<T: json_decode>
protected o<T> decode_as(const std.json::value* value) throws std.alloc::alloc_error {
    try {
        std.string::string text = std.json::stringify(value);
        T found = std.json::unmarshal(text);
        return o::some(move found);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return o::none;
}

/* R-SLIB-MCP-0017: tools/list: every tool over all pages. A client over HTTP leaves out tools
   whose x-mcp-header annotations are invalid and keeps the rest for the Mcp-Param headers of its
   calls. */
@scoped
async array<tool> client::list_tools(const client* this) throws mcp_error, std.error::fault {
    array<std.json::value> entries = std.array::create::<std.json::value>();
    task_scope(1) io {
        array<std.json::value> got = await this->list_all("tools/list", "tools");
        array<std.json::value> old = core::replace(&entries, move got);
        drop old;
    }
    array<tool> result = std.array::create::<tool>();
    array<tool> kept = std.array::create::<tool>();
    bool over_http = false;
    switch (this->http) {
    case variant o::some(link):
        link as void;
        over_http = true;
    case variant o::none: break;
    }
    for (usize index = 0usize; index < len(entries); index += 1usize) {
        o<tool> found = decode_as::<tool>(&entries[index]);
        switch (move found) {
        case variant o::some(move definition):
            if (over_http == true && usable_tool(&definition) == false) {
                drop definition;
                continue;
            }
            o<tool> copy = decode_as::<tool>(&entries[index]);
            switch (move copy) {
            case variant o::some(move second): append(&kept, move second);
            case variant o::none: break;
            }
            append(&result, move definition);
        case variant o::none: break;
        }
    }
    switch (this->http) {
    case variant o::some(link):
        task_scope(1) io {
            std.async::mutex_guard<array<tool>> guard = await link->tools.lock();
            array<tool> old = core::replace(std.async::mutex_guard_mut(&guard), move kept);
            drop old;
        }
    case variant o::none: drop kept;
    }
    return move result;
}

/* R-SLIB-MCP-0017: resources/list: every resource over all pages. */
@scoped
async array<resource> client::list_resources(const client* this) throws mcp_error, std.error::fault {
    array<std.json::value> entries = std.array::create::<std.json::value>();
    task_scope(1) io {
        array<std.json::value> got = await this->list_all("resources/list", "resources");
        array<std.json::value> old = core::replace(&entries, move got);
        drop old;
    }
    array<resource> result = std.array::create::<resource>();
    for (usize index = 0usize; index < len(entries); index += 1usize) {
        o<resource> found = decode_as::<resource>(&entries[index]);
        switch (move found) {
        case variant o::some(move item): append(&result, move item);
        case variant o::none: break;
        }
    }
    return move result;
}

/* R-SLIB-MCP-0017: resources/templates/list: every resource template over all pages. */
@scoped
async array<resource_template> client::list_templates(const client* this) throws mcp_error, std.error::fault {
    array<std.json::value> entries = std.array::create::<std.json::value>();
    task_scope(1) io {
        array<std.json::value> got = await this->list_all("resources/templates/list", "resourceTemplates");
        array<std.json::value> old = core::replace(&entries, move got);
        drop old;
    }
    array<resource_template> result = std.array::create::<resource_template>();
    for (usize index = 0usize; index < len(entries); index += 1usize) {
        o<resource_template> found = decode_as::<resource_template>(&entries[index]);
        switch (move found) {
        case variant o::some(move item): append(&result, move item);
        case variant o::none: break;
        }
    }
    return move result;
}

/* R-SLIB-MCP-0017: prompts/list: every prompt over all pages. */
@scoped
async array<prompt> client::list_prompts(const client* this) throws mcp_error, std.error::fault {
    array<std.json::value> entries = std.array::create::<std.json::value>();
    task_scope(1) io {
        array<std.json::value> got = await this->list_all("prompts/list", "prompts");
        array<std.json::value> old = core::replace(&entries, move got);
        drop old;
    }
    array<prompt> result = std.array::create::<prompt>();
    for (usize index = 0usize; index < len(entries); index += 1usize) {
        o<prompt> found = decode_as::<prompt>(&entries[index]);
        switch (move found) {
        case variant o::some(move item): append(&result, move item);
        case variant o::none: break;
        }
    }
    return move result;
}

protected std.json::value named_params(str key, str name, str arguments_key, std.json::value arguments)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value params = std.json::object();
    put_text(&params, key, name);
    const u8[] wanted = arguments_key;
    if (len(wanted) != 0usize) {
        put(&params, arguments_key, move arguments);
    } else {
        drop arguments;
    }
    return move params;
}

/* The params of a request by name or URI, with arguments under their key when it is not empty;
   null when they cannot be written. */
protected std.json::value built_params(str key, str name, str arguments_key, std.json::value arguments)
    throws std.alloc::alloc_error {
    try {
        return named_params(key, name, arguments_key, move arguments);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.json::null();
}

protected tool_result tool_result_of(const std.json::value* result) throws mcp_error, std.alloc::alloc_error {
    o<tool_result> found = decode_as::<tool_result>(result);
    switch (move found) {
    case variant o::some(move value): return move value;
    case variant o::none: break;
    }
    throw broken("tools/call returned no tool result");
}

/* The result of tools/call: a task (R-SLIB-MCP-0024) is followed to its end. */
@generic<S: send & sync & unborrowed>
@scoped
protected async tool_result client::called(const client* this, std.json::value body, const elicitor<S>* answers,
                                           bool elicit) throws mcp_error, std.error::fault {
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->round_trips("tools/call", move body, answers, elicit);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    if (result_kind(&result) == 2u8) {
        std.json::value created = core::replace(&result, std.json::null());
        task_scope(1) io {
            std.json::value ended = await this->follow_task(move created, answers);
            std.json::value old = core::replace(&result, move ended);
            drop old;
        }
    }
    return tool_result_of(&result);
}

/* R-SLIB-MCP-0017: tools/call with arguments, an object, answering the input requests of the
   server through the elicitor: the result of the tool, whose failure is a result with
   is_error. A tool that the server runs as a task is followed to its end (R-SLIB-MCP-0024). */
@generic<S: send & sync & unborrowed>
@scoped
async tool_result client::call_tool_with(const client* this, str name, std.json::value arguments, const elicitor<S>* answers)
    throws mcp_error, std.error::fault {
    std.json::value body = built_params("name", name, "arguments", move arguments);
    task_scope(1) io { return await this->called(move body, answers, true); }
}

/* R-SLIB-MCP-0017: tools/call without elicitation: a tool that needs input fails with -32021. */
@scoped
async tool_result client::call_tool(const client* this, str name, std.json::value arguments)
    throws mcp_error, std.error::fault {
    elicitor<nobody> none = no_input();
    std.json::value body = built_params("name", name, "arguments", move arguments);
    task_scope(1) io { return await this->called(move body, &none, false); }
}

/* R-SLIB-MCP-0024: the state of a task as a client sees it: its id, status and message, the
   interval at which to poll it, the input requests that wait for answers with their keys, the
   result of a completed task and the error of a failed one. */
struct task_state {
    std.string::string id;
    task_status status;
    std.string::string message;
    u64 poll_ms;
    array<std.string::string> keys;
    array<elicitation> requests;
    o<tool_result> result;
    o<mcp_error> error;
};

/* The state of a task from its JSON; input requests of no known kind are left out. */
protected task_state task_state_of(const std.json::value* value, const client_options* settings)
    throws mcp_error, std.alloc::alloc_error {
    task_state state = {.id = owned(member_text(value, "taskId")), .status = known_status(value),
                        .message = owned(member_text(value, "statusMessage")), .poll_ms = 1000u64,
                        .keys = std.array::create::<std.string::string>(),
                        .requests = std.array::create::<elicitation>(), .result = o::none, .error = o::none};
    throw (std.string::len(&state.id) == 0usize) broken("a task without taskId");
    switch (unsigned_member(value, "pollIntervalMs")) {
    case variant o::some(given): state.poll_ms = *given;
    case variant o::none: break;
    }
    if (state.status == task_status::input_required) {
        switch (member(value, "inputRequests")) {
        case variant o::some(requests):
            for (usize index = 0usize; index < std.json::len(*requests); index += 1usize) {
                str key = std.json::key_at(*requests, index);
                switch (std.json::find(*requests, key)) {
                case variant o::some(request):
                    try {
                        elicitation asked = elicitation_of(*request, settings);
                        add_text(&state.keys, key);
                        append(&state.requests, move asked);
                    } catch (std.json::error rejected) {
                        (move rejected) as void;
                    } catch (mcp_error rejected) {
                        (move rejected) as void;
                    }
                case variant o::none: break;
                }
            }
        case variant o::none: break;
        }
    }
    if (state.status == task_status::completed) {
        switch (member(value, "result")) {
        case variant o::some(result): state.result = o::some(tool_result_of(*result));
        case variant o::none: throw broken("a completed task without its result");
        }
    }
    if (state.status == task_status::failed) { state.error = o::some(task_error(value)); }
    return move state;
}

/* R-SLIB-MCP-0024: what tools/call started: the result of a tool that ran at once, or a task. */
enum tool_start { finished(tool_result), running(task_state) };

/* R-SLIB-MCP-0024: tools/call without waiting for a task: the result of a tool that runs at once,
   whose input requests the elicitor answers as call_tool_with does, or the first state of the
   task of a tool that the server runs as one for a client that declares the Tasks extension; the
   input requests of a task are answered with answer_task or finish_task. */
@generic<S: send & sync & unborrowed>
@scoped
async tool_start client::start_tool(const client* this, str name, std.json::value arguments, const elicitor<S>* answers)
    throws mcp_error, std.error::fault {
    std.json::value body = built_params("name", name, "arguments", move arguments);
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->round_trips("tools/call", move body, answers, true);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    if (result_kind(&result) == 2u8) { return tool_start::running(task_state_of(&result, &this->settings)); }
    return tool_start::finished(tool_result_of(&result));
}

/* R-SLIB-MCP-0024: tasks/get: the state of a task. */
@scoped
async task_state client::get_task(const client* this, str id) throws mcp_error, std.error::fault {
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->request("tasks/get", task_params(id), false);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    return task_state_of(&result, &this->settings);
}

/* R-SLIB-MCP-0024: tasks/update: the answer to the input request of a task under its key. */
@scoped
async void client::answer_task(const client* this, str id, str key, const answer* given)
    throws mcp_error, std.error::fault {
    std.json::value params = task_params(id);
    try {
        std.json::value responses = std.json::object();
        put(&responses, key, answer_value(given));
        put(&params, "inputResponses", move responses);
    } catch (std.json::error rejected) {
        (move rejected) as void;
        throw broken("the answer could not be written");
    }
    task_scope(1) io {
        std.json::value receipt = await this->request("tasks/update", move params, false);
        drop receipt;
    }
}

/* R-SLIB-MCP-0024: tasks/cancel: asks the server to cancel a task; the server decides when. */
@scoped
async void client::cancel_task(const client* this, str id) throws mcp_error, std.error::fault {
    task_scope(1) io {
        std.json::value receipt = await this->request("tasks/cancel", task_params(id), false);
        drop receipt;
    }
}

/* R-SLIB-MCP-0024: follows a task to its end as call_tool_with does: the result of its tool. */
@generic<S: send & sync & unborrowed>
@scoped
async tool_result client::finish_task(const client* this, str id, const elicitor<S>* answers)
    throws mcp_error, std.error::fault {
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->request("tasks/get", task_params(id), false);
        std.json::value ended = await this->follow_task(move got, answers);
        std.json::value old = core::replace(&result, move ended);
        drop old;
    }
    return tool_result_of(&result);
}

protected array<contents> contents_list(const std.json::value* result) throws mcp_error, std.alloc::alloc_error {
    array<contents> items = std.array::create::<contents>();
    switch (member(result, "contents")) {
    case variant o::some(listed):
        for (usize index = 0usize; index < std.json::len(*listed); index += 1usize) {
            switch (std.json::get(*listed, index)) {
            case variant o::some(item):
                try {
                    append(&items, contents_of(*item));
                } catch (std.json::error rejected) {
                    (move rejected) as void;
                    throw broken("resources/read returned contents of no known shape");
                }
            case variant o::none: break;
            }
        }
    case variant o::none: throw broken("resources/read returned no contents");
    }
    return move items;
}

/* R-SLIB-MCP-0017: resources/read, answering input requests through the elicitor: the contents
   of the resource. A resource that does not exist fails with -32602 (or -32002 from a server of
   an earlier revision). */
@generic<S: send & sync & unborrowed>
@scoped
async array<contents> client::read_resource_with(const client* this, str uri, const elicitor<S>* answers)
    throws mcp_error, std.error::fault {
    std.json::value body = built_params("uri", uri, "", std.json::null());
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->round_trips("resources/read", move body, answers, true);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    return contents_list(&result);
}

/* R-SLIB-MCP-0017: resources/read without elicitation. */
@scoped
async array<contents> client::read_resource(const client* this, str uri) throws mcp_error, std.error::fault {
    elicitor<nobody> none = no_input();
    std.json::value body = built_params("uri", uri, "", std.json::null());
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->round_trips("resources/read", move body, &none, false);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    return contents_list(&result);
}

protected prompt_result prompt_result_of(const std.json::value* result) throws mcp_error, std.alloc::alloc_error {
    o<prompt_result> found = decode_as::<prompt_result>(result);
    switch (move found) {
    case variant o::some(move value): return move value;
    case variant o::none: break;
    }
    throw broken("prompts/get returned no prompt");
}

/* R-SLIB-MCP-0017: prompts/get with arguments, an object of strings, answering input requests
   through the elicitor. */
@generic<S: send & sync & unborrowed>
@scoped
async prompt_result client::get_prompt_with(const client* this, str name, std.json::value arguments,
                                            const elicitor<S>* answers)
    throws mcp_error, std.error::fault {
    std.json::value body = built_params("name", name, "arguments", move arguments);
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->round_trips("prompts/get", move body, answers, true);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    return prompt_result_of(&result);
}

/* R-SLIB-MCP-0017: prompts/get without elicitation. */
@scoped
async prompt_result client::get_prompt(const client* this, str name, std.json::value arguments)
    throws mcp_error, std.error::fault {
    elicitor<nobody> none = no_input();
    std.json::value body = built_params("name", name, "arguments", move arguments);
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->round_trips("prompts/get", move body, &none, false);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    return prompt_result_of(&result);
}

protected std.json::value completion_params(bool prompt, str reference, str argument, str value)
    throws std.alloc::alloc_error {
    try {
        std.json::value target = std.json::object();
        if (prompt == true) {
            put_text(&target, "type", "ref/prompt");
            put_text(&target, "name", reference);
        } else {
            put_text(&target, "type", "ref/resource");
            put_text(&target, "uri", reference);
        }
        std.json::value given = std.json::object();
        put_text(&given, "name", argument);
        put_text(&given, "value", value);
        std.json::value built = std.json::object();
        put(&built, "ref", move target);
        put(&built, "argument", move given);
        return move built;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return std.json::null();
}

/* R-SLIB-MCP-0017: completion/complete of an argument of a prompt (prompt is true, reference is
   its name) or of a variable of a resource template (reference is the template) from its value
   so far. */
@scoped
async completion client::complete(const client* this, bool prompt, str reference, str argument, str value)
    throws mcp_error, std.error::fault {
    std.json::value body = completion_params(prompt, reference, argument, value);
    std.json::value result = std.json::null();
    task_scope(1) io {
        std.json::value got = await this->request("completion/complete", move body, false);
        std.json::value old = core::replace(&result, move got);
        drop old;
    }
    switch (member(&result, "completion")) {
    case variant o::some(found):
        o<completion> parsed = decode_as::<completion>(*found);
        switch (move parsed) {
        case variant o::some(move answer_found): return move answer_found;
        case variant o::none: break;
        }
    case variant o::none: break;
    }
    throw broken("completion/complete returned no completion");
}

/* R-SLIB-MCP-0018: a change that a subscription of a client reports: (R-SLIB-MCP-0023) state is
   the new state of a task. */
enum change { tools, prompts, resources, updated(std.string::string), state(task_state) };

/* R-SLIB-MCP-0018: the notifications that a subscription asks for, and those that the server
   honours: list changes of tools, prompts and resources, updates of the resources at URIs (and
   below them) and (R-SLIB-MCP-0023) the states of the tasks with the ids in tasks. */
struct interests {
    bool tools;
    bool prompts;
    bool resources;
    array<std.string::string> uris;
    array<std.string::string> tasks;
};

interests interests::create() {
    return interests {.tools = false, .prompts = false, .resources = false,
                      .uris = std.array::create::<std.string::string>(),
                      .tasks = std.array::create::<std.string::string>()};
}

protected std.json::value interests_value(const interests* wanted) throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    if (wanted->tools == true) { put_flag(&result, "toolsListChanged"); }
    if (wanted->prompts == true) { put_flag(&result, "promptsListChanged"); }
    if (wanted->resources == true) { put_flag(&result, "resourcesListChanged"); }
    if (len(wanted->uris) != 0usize) {
        std.json::value uris = std.json::array();
        for (usize index = 0usize; index < len(wanted->uris); index += 1usize) {
            std.json::append(&uris, std.json::from_string(wanted->uris[index]));
        }
        put(&result, "resourceSubscriptions", move uris);
    }
    if (len(wanted->tasks) != 0usize) {
        std.json::value ids = std.json::array();
        for (usize index = 0usize; index < len(wanted->tasks); index += 1usize) {
            std.json::append(&ids, std.json::from_string(wanted->tasks[index]));
        }
        put(&result, "taskIds", move ids);
    }
    return move result;
}

protected interests interests_of(const std.json::value* notifications) throws std.alloc::alloc_error {
    interests result = interests::create();
    result.tools = member_true(notifications, "toolsListChanged");
    result.prompts = member_true(notifications, "promptsListChanged");
    result.resources = member_true(notifications, "resourcesListChanged");
    switch (member(notifications, "resourceSubscriptions")) {
    case variant o::some(uris):
        for (usize index = 0usize; index < std.json::len(*uris); index += 1usize) {
            switch (std.json::get(*uris, index)) {
            case variant o::some(item): add_text(&result.uris, std.json::text(*item));
            case variant o::none: break;
            }
        }
    case variant o::none: break;
    }
    switch (member(notifications, "taskIds")) {
    case variant o::some(ids):
        array<std.string::string> found = texts_of(*ids);
        array<std.string::string> old = core::replace(&result.tasks, move found);
        drop old;
    case variant o::none: break;
    }
    return move result;
}

/* R-SLIB-MCP-0018: a subscription of a client: the notifications that the server honours, and
   the stream of its changes, which next reads. */
struct subscription {
    interests granted;
    protected std.jsonrpc::request_id id;
    protected o<std.sync::receiver<std.jsonrpc::message>> inbox;
    protected o<std.http::streamed> stream;
    protected std.http::sse_parser parser;
    protected bool ended;
};

/* What a message of a subscription means: a change, its acknowledgement, its end, or nothing. */
protected enum event { changed(change), acknowledged(interests), ended, other };

protected event event_of(std.jsonrpc::message message) throws mcp_error, std.alloc::alloc_error {
    switch (move message) {
    case variant std.jsonrpc::message::notification(move note):
        str method = note.method;
        if (std.bytes::equal(method, "notifications/tools/list_changed") == true) { return event::changed(change::tools); }
        if (std.bytes::equal(method, "notifications/prompts/list_changed") == true) { return event::changed(change::prompts); }
        if (std.bytes::equal(method, "notifications/resources/list_changed") == true) {
            return event::changed(change::resources);
        }
        if (std.bytes::equal(method, "notifications/cancelled") == true) { return event::ended; }
        switch (note.params) {
        case variant o::some(params):
            if (std.bytes::equal(method, "notifications/resources/updated") == true) {
                return event::changed(change::updated(owned(member_text(params, "uri"))));
            }
            if (std.bytes::equal(method, "notifications/tasks") == true) {
                client_options modes = {.form = true, .url = true};
                return event::changed(change::state(task_state_of(params, &modes)));
            }
            if (std.bytes::equal(method, "notifications/subscriptions/acknowledged") == true) {
                switch (member(params, "notifications")) {
                case variant o::some(granted): return event::acknowledged(interests_of(*granted));
                case variant o::none: return event::acknowledged(interests::create());
                }
            }
        case variant o::none: break;
        }
        return event::other;
    case variant std.jsonrpc::message::failure(move item):
        throw mcp_error {.code = item.code, .message = std.string::from_str(item.message)};
    case variant std.jsonrpc::message::result(move item):
        drop item;
        return event::ended;
    case variant std.jsonrpc::message::request(move item):
        drop item;
        return event::other;
    }
    return event::other;
}

/* The next message of a subscription over HTTP: its events are read as they arrive; none at the
   end of the stream. */
@scoped
protected async o<std.jsonrpc::message> next_event(std.http::streamed* stream, std.http::sse_parser* parser)
    throws std.http::http_error, std.error::fault {
    while (true) {
        o<std.http::sse_message> ready = parser->next();
        switch (move ready) {
        case variant o::some(move found):
            try {
                return o::some(std.jsonrpc::parse(found.data));
            } catch (std.jsonrpc::rpc_error rejected) {
                (move rejected) as void;
            }
        case variant o::none:
            o<bytes> arrived = o::none;
            task_scope(1) io {
                o<bytes> got = await stream->next();
                o<bytes> old = core::replace(&arrived, move got);
                drop old;
            }
            switch (move arrived) {
            case variant o::some(move data): parser->feed(data.as_slice());
            case variant o::none: return o::none;
            }
        }
    }
    return o::none;
}

/* R-SLIB-MCP-0018: the next change of the subscription, none when the subscription has ended:
   the server closed it, or its connection ended. Messages that are no changes are skipped. */
@scoped
async o<change> subscription::next(subscription* this) throws mcp_error, std.error::fault {
    while (this->ended == false) {
        o<std.jsonrpc::message> message = o::none;
        o<std.http::streamed> events = core::replace(&this->stream, o::none);
        switch (move events) {
        case variant o::some(move stream):
            try {
                task_scope(1) io {
                    o<std.jsonrpc::message> got = await next_event(&stream, &this->parser);
                    o<std.jsonrpc::message> old = core::replace(&message, move got);
                    drop old;
                }
            } catch (std.http::http_error rejected) {
                rejected as void;
            }
            o<std.http::streamed> empty = core::replace(&this->stream, o::some(move stream));
            drop empty;
        case variant o::none: break;
        }
        o<std.sync::receiver<std.jsonrpc::message>> taken = core::replace(&this->inbox, o::none);
        switch (move taken) {
        case variant o::some(move inbox):
            task_scope(1) io {
                o<std.jsonrpc::message> got = await inbox.receive();
                o<std.jsonrpc::message> old = core::replace(&message, move got);
                drop old;
            }
            o<std.sync::receiver<std.jsonrpc::message>> empty = core::replace(&this->inbox, o::some(move inbox));
            drop empty;
        case variant o::none: break;
        }
        switch (move message) {
        case variant o::some(move item):
            event meaning = event_of(move item);
            switch (move meaning) {
            case variant event::changed(move found): return o::some(move found);
            case variant event::ended: this->ended = true;
            default: break;
            }
        case variant o::none: this->ended = true;
        }
    }
    return o::none;
}

/* The acknowledgement of a subscription: the first message about it, within the timeout. */
@scoped
protected async interests acknowledgement(std.sync::receiver<std.jsonrpc::message>* inbox, std.time::duration timeout)
    throws mcp_error, std.error::fault {
    std.time::instant now = std.time::monotonic_now();
    std.time::instant limit = std.time::instant_add(now, timeout);
    o<std.jsonrpc::message> first = o::none;
    bool expired = false;
    task_scope(1) wait {
        auto next = inbox->receive();
        select (wait) {
        case o<std.jsonrpc::message> got = await move next:
            o<std.jsonrpc::message> old = core::replace(&first, move got);
            drop old;
        case until (limit): expired = true;
        }
        wait.cancel_all();
    }
    limit as void;
    throw (expired == true) failure(request_timeout, "the subscription got no acknowledgement in time");
    switch (move first) {
    case variant o::some(move message):
        event meaning = event_of(move message);
        switch (move meaning) {
        case variant event::acknowledged(move granted): return move granted;
        default: break;
        }
        throw broken("a subscription began without its acknowledgement");
    case variant o::none: break;
    }
    throw closed_error();
}

/* A subscription over a stream: its waiter, its request and its acknowledgement. */
@scoped
protected async subscription listen_stream(const stream_link* link, std.jsonrpc::request message, std.time::duration timeout)
    throws mcp_error, std.error::fault {
    std.jsonrpc::request_id id = message.id.copy();
    std.sync::sync_channel<std.jsonrpc::message> factory = std.sync::sync_channel::<std.jsonrpc::message>(64usize);
    std.sync::sync_sender<std.jsonrpc::message> sender = std.sync::sync_sender(&factory);
    std.sync::receiver<std.jsonrpc::message> inbox = std.sync::sync_receiver(move factory);
    waiter entry = {.id = id.copy(), .sender = move sender};
    task_scope(1) io { await add_waiter(&link->waiters, move entry); }
    std.jsonrpc::message request_message = std.jsonrpc::message::request(move message);
    try {
        task_scope(1) io { await write_to(&link->output, &request_message); }
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    drop request_message;
    interests granted = interests::create();
    try {
        task_scope(1) io {
            interests found = await acknowledgement(&inbox, timeout);
            interests old = core::replace(&granted, move found);
            drop old;
        }
    } catch (mcp_error rejected) {
        task_scope(1) io { await remove_waiter(&link->waiters, &id); }
        throw move rejected;
    }
    return subscription {.granted = move granted, .id = move id, .inbox = o::some(move inbox), .stream = o::none,
                         .parser = std.http::sse_parser::create(), .ended = false};
}

/* A subscription over HTTP: the event stream of its POST and its acknowledgement. */
@scoped
protected async subscription listen_http(const http_link* link, std.jsonrpc::request message, std.time::duration timeout)
    throws mcp_error, std.error::fault {
    std.jsonrpc::request_id id = message.id.copy();
    std.jsonrpc::message request_message = std.jsonrpc::message::request(move message);
    o<std.string::string> encoded = encoded_text(&request_message);
    drop request_message;
    std.string::string text = text_or_empty(move encoded);
    throw (std.string::len(&text) == 0usize) broken("the request could not be written");
    std.http::request post = std.http::request::create(std.http::method::post, "/");
    try {
        post.headers.add("Content-Type", "application/json");
        post.headers.add("Accept", "text/event-stream, application/json");
        post.headers.add("MCP-Protocol-Version", protocol_version);
        post.headers.add("Mcp-Method", "subscriptions/listen");
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    std.bytes::append(&post.body, text);
    add_token(link, &post);
    std.http::client web = web_client(link);
    o<std.http::streamed> opened = o::none;
    std.time::instant now = std.time::monotonic_now();
    std.time::instant limit = std.time::instant_add(now, timeout);
    try {
        deadline (limit) {
            task_scope(1) io {
                std.http::streamed got = await web.open(move post, link->endpoint);
                o<std.http::streamed> old = core::replace(&opened, o::some(move got));
                drop old;
            }
        }
    } catch (std.http::http_error rejected) {
        rejected as void;
        throw closed_error();
    } catch (std.tls::tls_error rejected) {
        rejected as void;
        throw closed_error();
    }
    switch (move opened) {
    case variant o::some(move stream):
        std.http::sse_parser parser = std.http::sse_parser::create();
        o<std.jsonrpc::message> first = o::none;
        if (stream.head.status == 200u16) {
            try {
                deadline (limit) {
                    task_scope(1) io {
                        o<std.jsonrpc::message> got = await next_event(&stream, &parser);
                        o<std.jsonrpc::message> old = core::replace(&first, move got);
                        drop old;
                    }
                }
            } catch (std.http::http_error rejected) {
                rejected as void;
            }
        } else {
            try {
                task_scope(1) io {
                    o<std.jsonrpc::message> got = await json_message(&stream, 65536usize);
                    o<std.jsonrpc::message> old = core::replace(&first, move got);
                    drop old;
                }
            } catch (std.http::http_error rejected) {
                rejected as void;
            }
        }
        switch (move first) {
        case variant o::some(move item):
            event meaning = event_of(move item);
            switch (move meaning) {
            case variant event::acknowledged(move granted):
                return subscription {.granted = move granted, .id = move id, .inbox = o::none,
                                     .stream = o::some(move stream), .parser = move parser, .ended = false};
            default: break;
            }
            throw broken("a subscription began without its acknowledgement");
        case variant o::none: break;
        }
        throw closed_error();
    case variant o::none: break;
    }
    throw closed_error();
}

/* R-SLIB-MCP-0018: subscriptions/listen: a subscription to the notifications that the server
   honours of those wanted, as its acknowledgement grants them. */
@scoped
async subscription client::listen(const client* this, const interests* wanted) throws mcp_error, std.error::fault {
    std.jsonrpc::request_id id = std.jsonrpc::request_id::from_integer(0i64);
    task_scope(1) io {
        std.jsonrpc::request_id next = await this->next_id();
        std.jsonrpc::request_id old = core::replace(&id, move next);
        drop old;
    }
    o<std.json::value> prepared = o::none;
    try {
        std.json::value params = std.json::object();
        put(&params, "notifications", interests_value(wanted));
        std.json::value full = with_meta(move params, &this->info, &this->settings, false, &id);
        o<std.json::value> old = core::replace(&prepared, o::some(move full));
        drop old;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    std.json::value body = value_or_null(move prepared);
    std.jsonrpc::request message = {.id = move id, .method = std.string::from_str("subscriptions/listen"),
                                    .params = o::some(move body)};
    switch (this->streams) {
    case variant o::some(link):
        task_scope(1) io { return await listen_stream(link, move message, this->settings.timeout); }
    case variant o::none:
        switch (this->http) {
        case variant o::some(link):
            task_scope(1) io { return await listen_http(link, move message, this->settings.timeout); }
        case variant o::none: drop message;
        }
    }
    throw closed_error();
}

/* R-SLIB-MCP-0018: ends a subscription: over a stream the client sends notifications/cancelled,
   over HTTP it closes the event stream. */
@scoped
async void client::unlisten(const client* this, subscription watched) throws std.error::fault {
    switch (this->streams) {
    case variant o::some(link):
        task_scope(1) io {
            await cancel_on(link, &watched.id, "unsubscribed");
            await remove_waiter(&link->waiters, &watched.id);
        }
    case variant o::none: break;
    }
    drop watched;
}

/* R-SLIB-MCP-0019: a server started as a child process and a client over its standard input and
   output; the child, which the program waits for after close, is taken out with core::replace. */
struct launched { client link; o<std.process::child> child; };

/* R-SLIB-MCP-0019: starts the command with piped standard input and output, its standard error
   inherited, and returns the client of the server it runs. The client runs over those pipes:
   run reads the output, close ends the input, which a server takes as the signal to finish. */
async launched launch(std.process::command command, implementation info, client_options settings)
    throws std.error::fault {
    std.process::stdio policy = {.input = std.process::pipe_mode::piped, .output = std.process::pipe_mode::piped,
                                 .error = std.process::pipe_mode::inherit};
    command.set_stdio(policy);
    std.process::spawn_result started = await (move command).spawn();
    switch (move started) {
    case variant std.process::spawn_result::failed(move failed):
        throw failed.error;
    case variant std.process::spawn_result::spawned(move child):
        o<std.io::output> input_pipe = child.take_stdin();
        o<std.io::input> output_pipe = child.take_stdout();
        switch (move input_pipe) {
        case variant o::some(move writer):
            switch (move output_pipe) {
            case variant o::some(move reader):
                own dyn(std.stream::Reader)* input = new std.io::input(move reader);
                own dyn(std.stream::Writer)* output = new std.io::output(move writer);
                client link = client::over_streams(move input, move output, move info, settings);
                return launched {.link = move link, .child = o::some(move child)};
            case variant o::none:
                drop writer;
                drop child;
            }
        case variant o::none:
            drop output_pipe;
            drop child;
        }
    }
    throw std.io::io_error {.code = std.io::error_code::closed, .native_code = 0i64};
}
