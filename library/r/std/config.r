module std.config;
import std.args;
import std.text;

/* R-SLIB-CONFIG-0001: where the current value of a key came from; later layers win. */
enum source { default_value, file, environment, arguments };

@derive(format)
enum error_code {
    invalid_key,
    duplicate_key,
    unknown_key,
    invalid_value,
    invalid_file,
    missing_value,
};

/* R-SLIB-CONFIG-0001..0003: `index` is the declaration of the key, or the byte offset in the
   file for invalid_file. */
error config_error {
    error_code code;
    usize index;
};

protected struct entry {
    std.string::string key;
    std.string::string value;
    std.string::string help;
    source origin;
};

/* R-SLIB-CONFIG-0001: the declared keys of a program with their current values. */
struct config {
    protected array<entry> entries;
};

/* An object of the file still to flatten, with the key prefix of its members. */
protected struct pending {
    std.json::value node;
    std.string::string prefix;
};

/* A value of the file for the declaration at `index`, applied when the whole file is valid. */
protected struct update {
    usize index;
    std.string::string value;
};

@generic<T>
protected void append(array<T>* target, T value) throws std.alloc::alloc_error {
    try {
        target->push(move value);
    } catch (std.array::push_error<T> failure) {
        switch (move failure) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

protected bool same(const u8[] left, const u8[] right) {
    return std.bytes::equal(left, right);
}

/* A key is 1..64 lowercase ASCII letters, digits and `_`, in `.`-separated segments. */
protected bool valid_key(const u8[] bytes) {
    if (len(bytes) == 0usize || len(bytes) > 64usize) { return false; }
    bool segment_start = true;
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 byte = bytes[index];
        if (byte == 46u8) {
            if (segment_start == true) { return false; }
            segment_start = true;
        } else {
            if ((byte < 97u8 || byte > 122u8) && (byte < 48u8 || byte > 57u8) && byte != 95u8) {
                return false;
            }
            segment_start = false;
        }
    }
    return segment_start == false;
}

/* R-SLIB-CONFIG-0001: a configuration without keys. */
config config::create() {
    return config {.entries = std.array::create()};
}

/* R-SLIB-CONFIG-0001: declares a key with its default value and help. */
void config::define(config* this, str key, str default_value, str help)
    throws config_error, std.alloc::alloc_error {
    usize index = len(this->entries);
    throw (valid_key(key) == false) config_error {.code = error_code::invalid_key, .index = index};
    for (const entry* existing in &this->entries) {
        throw (same(existing->key, key) == true)
            config_error {.code = error_code::duplicate_key, .index = index};
    }
    append(&this->entries, entry {.key = std.string::from_str(key),
                                  .value = std.string::from_str(default_value),
                                  .help = std.string::from_str(help),
                                  .origin = source::default_value});
}

protected o<usize> config::find(const config* this, const u8[] key) {
    for (usize index = 0usize; index < len(this->entries); index += 1usize) {
        if (same(this->entries[index].key, key) == true) { return o::some(index); }
    }
    return o::none;
}

protected usize config::index_of(const config* this, const u8[] key) throws config_error {
    o<usize> found = this->find(key);
    switch (found) {
    case variant o::some(index): return *index;
    case variant o::none: break;
    }
    throw config_error {.code = error_code::unknown_key, .index = 0usize};
}

/* R-SLIB-CONFIG-0002: sets the value of a declared key from a layer. */
void config::set(config* this, str key, str value, source origin)
    throws config_error, std.alloc::alloc_error {
    usize index = this->index_of(key);
    this->entries[index].value = std.string::from_str(value);
    this->entries[index].origin = origin;
}

/* Flattens a JSON object into dotted keys without recursion, taking each member out of its
   object: strings and numbers give their text, true and false their spelling, null leaves the
   key unchanged, arrays are invalid. Nothing changes unless every member is valid. */
protected void config::apply_json(config* this, std.json::value root)
    throws config_error, std.json::error, std.alloc::alloc_error {
    array<pending> work = std.array::create();
    array<update> updates = std.array::create();
    append(&work, pending {.node = move root, .prefix = std.string::create()});
    while (len(work) != 0usize) {
        o<pending> next = work.pop();
        switch (move next) {
        case variant o::none: break;
        case variant o::some(move item):
            array<std.string::string> names = std.array::create();
            usize count = std.json::len(&item.node);
            for (usize index = 0usize; index < count; index += 1usize) {
                append(&names, std.string::from_str(std.json::key_at(&item.node, index)));
            }
            for (const std.string::string* name in &names) {
                std.string::string key = std.string::from_str(item.prefix);
                if (std.string::len(&key) != 0usize) { key.append("."); }
                key.append(*name);
                std.json::value member = std.json::take_field(&item.node, *name);
                std.json::value_kind kind = std.json::kind(&member);
                if (kind == std.json::value_kind::object) {
                    append(&work, pending {.node = move member, .prefix = move key});
                } else {
                    this->collect(&updates, key, &member);
                    drop member;
                    drop key;
                }
            }
        }
    }
    for (usize index = 0usize; index < len(updates); index += 1usize) {
        usize at = updates[index].index;
        this->entries[at].value = core::replace(&updates[index].value, std.string::create());
        this->entries[at].origin = source::file;
    }
}

protected void config::collect(const config* this, array<update>* updates, str key,
                               const std.json::value* value)
    throws config_error, std.alloc::alloc_error {
    usize at = this->index_of(key);
    std.json::value_kind kind = std.json::kind(value);
    if (kind == std.json::value_kind::null) {
        at as void;
        return;
    }
    if (kind == std.json::value_kind::string || kind == std.json::value_kind::number) {
        append(updates, update {.index = at, .value = std.string::from_str(std.json::text(value))});
        return;
    }
    if (kind == std.json::value_kind::boolean) {
        if (std.json::boolean(value) == true) {
            append(updates, update {.index = at, .value = std.string::from_str("true")});
        } else {
            append(updates, update {.index = at, .value = std.string::from_str("false")});
        }
        return;
    }
    throw config_error {.code = error_code::invalid_value, .index = at};
}

/* R-SLIB-CONFIG-0002: the JSON object of the file at `path`, of at most 1 MiB. A key of the
   file that no declaration names is unknown_key; a document that is not a JSON object is
   invalid_file at the byte offset of the problem. */
@scoped
async void config::load_file(config* this, const std.fs::path* path)
    throws config_error, std.error::fault {
    std.fs::path file = std.fs::path_clone(path);
    bytes content = await std.fs::read_file(&file, 1048576usize);
    try {
        std.json::value document = std.json::parse(content.as_slice());
        throw (std.json::kind(&document) != std.json::value_kind::object)
            config_error {.code = error_code::invalid_file, .index = 0usize};
        this->apply_json(move document);
    } catch (std.json::error failure) {
        throw config_error {.code = error_code::invalid_file, .index = failure.offset};
    }
}

/* The environment variable of a key: the prefix, `_`, and the key in upper case with `.` as
   `_`. */
protected std.string::string variable_name(str prefix, str key) throws std.alloc::alloc_error {
    std.string::string upper = std.text::ascii_uppercase(key);
    std.string::string joined = std.text::replace(upper, ".", "_");
    return f"{prefix}_{joined}";
}

/* R-SLIB-CONFIG-0002: the variables `PREFIX_KEY` of the declared keys that are set. */
void config::load_environment(config* this, str prefix) throws std.error::fault {
    for (usize index = 0usize; index < len(this->entries); index += 1usize) {
        std.string::string name = variable_name(prefix, this->entries[index].key);
        o<std.string::string> value = std.env::get(name);
        switch (move value) {
        case variant o::some(move text):
            this->entries[index].value = move text;
            this->entries[index].origin = source::environment;
        case variant o::none: break;
        }
    }
}

/* R-SLIB-CONFIG-0002: the given flags, options and positional arguments named like declared
   keys; a flag gives `true`. */
void config::load_arguments(config* this, const std.args::matches* matches)
    throws std.alloc::alloc_error {
    for (usize index = 0usize; index < len(this->entries); index += 1usize) {
        try {
            str key = this->entries[index].key;
            if (matches->has(key) == true) {
                o<std.string::string> value = matches->value(key);
                switch (move value) {
                case variant o::some(move text): this->entries[index].value = move text;
                case variant o::none: this->entries[index].value = std.string::from_str("true");
                }
                this->entries[index].origin = source::arguments;
            }
        } catch (std.args::args_error failure) {
            failure as void;
        }
    }
}

/* R-SLIB-CONFIG-0003: the current text of a declared key. */
str config::get(const config* this, str key) throws config_error {
    usize index = this->index_of(key);
    return this->entries[index].value;
}

/* R-SLIB-CONFIG-0003: where the current value of a declared key came from. */
source config::source_of(const config* this, str key) throws config_error {
    usize index = this->index_of(key);
    return this->entries[index].origin;
}

/* R-SLIB-CONFIG-0003: the value as a decimal integer, invalid_value when it is not one. */
i64 config::get_i64(const config* this, str key) throws config_error {
    usize index = this->index_of(key);
    try {
        return std.convert::parse_i64(this->entries[index].value, 10u32);
    } catch (std.convert::parse_error failure) {
        failure as void;
    }
    throw config_error {.code = error_code::invalid_value, .index = index};
}

u64 config::get_u64(const config* this, str key) throws config_error {
    usize index = this->index_of(key);
    try {
        return std.convert::parse_u64(this->entries[index].value, 10u32);
    } catch (std.convert::parse_error failure) {
        failure as void;
    }
    throw config_error {.code = error_code::invalid_value, .index = index};
}

/* R-SLIB-CONFIG-0003: `true` or `false`, anything else invalid_value. */
bool config::get_bool(const config* this, str key) throws config_error {
    usize index = this->index_of(key);
    str text = this->entries[index].value;
    if (same(text, "true") == true) { return true; }
    throw (same(text, "false") == false)
        config_error {.code = error_code::invalid_value, .index = index};
    return false;
}

/* Whether a key names a secret: one of its segments contains password, secret or token. */
protected bool sensitive(str key) {
    return std.text::contains(key, "password") == true || std.text::contains(key, "secret") == true ||
           std.text::contains(key, "token") == true;
}

/* R-SLIB-CONFIG-0003: one line per key with its value and source; the value of a key whose
   name contains password, secret or token is written as `***`. */
std.string::string config::describe(const config* this) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (const entry* item in &this->entries) {
        str origin = core::enum_name(item->origin);
        if (sensitive(item->key) == true) {
            std.string::string line = f"{item->key}=*** ({origin})\n";
            text.append(line);
        } else {
            std.string::string line = f"{item->key}={item->value} ({origin})\n";
            text.append(line);
        }
    }
    return move text;
}

/* ---- Typed settings (R-SLIB-CONFIG-0004) ---- */

/* R-SLIB-CONFIG-0004: a setting that a typed read could not fill: the environment variable or
   the key it names, and whether it was missing or did not convert. */
error field_error { error_code code; std.string::string name; };

protected field_error field_failure(error_code code, str name) throws std.alloc::alloc_error {
    return field_error {.code = code, .name = std.string::from_str(name)};
}

/* The member `name` of a schema object, or none. */
protected o<const std.json::value*> schema_member(const std.json::value* object, str name) {
    const u8[] key = name;
    return std.json::find(object, key);
}

protected bool text_member_is(const std.json::value* object, str name, str expected) {
    switch (schema_member(object, name)) {
    case variant o::some(found):
        if (std.json::kind(*found) != std.json::value_kind::string) { return false; }
        str text = std.json::text(*found);
        const u8[] text_bytes = text;
        const u8[] wanted = expected;
        return same(text_bytes, wanted);
    case variant o::none: return false;
    }
}

/* Whether a property accepts null: anyOf with a null alternative, as o<T> writes. */
protected bool nullable(const std.json::value* property) {
    switch (schema_member(property, "anyOf")) {
    case variant o::some(choices):
        for (usize index = 0usize; index < std.json::len(*choices); index += 1usize) {
            switch (std.json::get(*choices, index)) {
            case variant o::some(choice):
                if (text_member_is(*choice, "type", "null") == true) { return true; }
            case variant o::none: break;
            }
        }
        return false;
    case variant o::none: return false;
    }
}

/* The alternative of a nullable property that is not null, or the property itself. */
protected const std.json::value* value_schema(const std.json::value* property) {
    switch (schema_member(property, "anyOf")) {
    case variant o::some(choices):
        for (usize index = 0usize; index < std.json::len(*choices); index += 1usize) {
            switch (std.json::get(*choices, index)) {
            case variant o::some(choice):
                if (text_member_is(*choice, "type", "null") == false) { return *choice; }
            case variant o::none: break;
            }
        }
        return property;
    case variant o::none: return property;
    }
}

protected std.string::string trimmed(str text) throws std.alloc::alloc_error {
    return std.string::from_str(std.text::trim(text));
}

/* The JSON value of the text of a scalar for its schema: strings as they are, integers in
   decimal within the bounds of the type, numbers, booleans as true/false/1/0/yes/no/on/off in any
   case, the names of enums, and other types as JSON text. */
protected std.json::value scalar_value(const std.json::value* described, str name, str text)
    throws field_error, std.alloc::alloc_error {
    try {
        if (text_member_is(described, "type", "string") == true) {
            const u8[] raw_text = text;
            return std.json::from_string(raw_text);
        }
        if (text_member_is(described, "type", "boolean") == true) {
            std.string::string lower = std.text::ascii_lowercase(std.text::trim(text));
            str word = lower;
            switch (word) {
            case "true": return std.json::from_bool(true);
            case "1": return std.json::from_bool(true);
            case "yes": return std.json::from_bool(true);
            case "on": return std.json::from_bool(true);
            case "false": return std.json::from_bool(false);
            case "0": return std.json::from_bool(false);
            case "no": return std.json::from_bool(false);
            case "off": return std.json::from_bool(false);
            default: throw field_failure(error_code::invalid_value, name);
            }
        }
        if (text_member_is(described, "type", "integer") == true) {
            std.string::string digits = trimmed(text);
            const u8[] digit_bytes = digits;
            if (len(digit_bytes) > 0usize && digit_bytes[0usize] == 45u8) {
                i64 parsed = std.convert::parse_i64(digits, 10u32);
                switch (schema_member(described, "minimum")) {
                case variant o::some(minimum):
                    i64 least = std.convert::parse_i64(std.json::text(*minimum), 10u32);
                    throw (parsed < least) field_failure(error_code::invalid_value, name);
                case variant o::none: parsed as void;
                }
            } else {
                u64 parsed = std.convert::parse_u64(digits, 10u32);
                switch (schema_member(described, "maximum")) {
                case variant o::some(maximum):
                    u64 most = std.convert::parse_u64(std.json::text(*maximum), 10u32);
                    throw (parsed > most) field_failure(error_code::invalid_value, name);
                case variant o::none: parsed as void;
                }
            }
            std.json::number number = std.json::parse_number(digits);
            return std.json::from_number(&number);
        }
        if (text_member_is(described, "type", "number") == true) {
            std.string::string digits = trimmed(text);
            std.json::number number = std.json::parse_number(digits);
            return std.json::from_number(&number);
        }
        const u8[] json_text = text;
        switch (schema_member(described, "enum")) {
        case variant o::some(_): return std.json::from_string(json_text);
        case variant o::none: break;
        }
        return std.json::parse(json_text);
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw field_failure(error_code::invalid_value, name);
}

/* The JSON value of a setting's text for the schema of its property: a scalar of scalar_value,
   or an array of such items separated by commas. */
protected std.json::value converted(const std.json::value* property, str name, str text)
    throws field_error, std.alloc::alloc_error {
    const std.json::value* described = value_schema(property);
    if (text_member_is(described, "type", "array") == false) { return scalar_value(described, name, text); }
    try {
        std.json::value items = std.json::array();
        std.string::string whole = trimmed(text);
        const u8[] all = whole;
        if (len(all) == 0usize) { return move items; }
        switch (schema_member(described, "items")) {
        case variant o::some(item_schema):
            usize start = 0usize;
            for (usize index = 0usize; index <= len(all); index += 1usize) {
                if (index == len(all) || all[index] == 44u8) {
                    str piece = core::validate_utf8(all[start..index]);
                    std.string::string item = trimmed(piece);
                    std.json::append(&items, scalar_value(*item_schema, name, item));
                    start = index + 1usize;
                }
            }
            return move items;
        case variant o::none: drop items;
        }
    } catch (std.json::error rejected) {
        (move rejected) as void;
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    throw field_failure(error_code::invalid_value, name);
}

/* Whether the schema of T requires a property. */
protected bool required_by(const std.json::value* described, str key) {
    const u8[] wanted = key;
    switch (schema_member(described, "required")) {
    case variant o::some(names):
        for (usize index = 0usize; index < std.json::len(*names); index += 1usize) {
            switch (std.json::get(*names, index)) {
            case variant o::some(found):
                str text = std.json::text(*found);
                const u8[] text_bytes = text;
                if (same(text_bytes, wanted) == true) { return true; }
            case variant o::none: break;
            }
        }
        return false;
    case variant o::none: return false;
    }
}

/* Places the value of one property in the document: converted when it has a text, null when
   it is absent and accepts null, missing_value when it is absent and required. */
protected void place(std.json::value* document, const std.json::value* described, str key, const std.json::value* property,
                     str name, o<std.string::string> text) throws field_error, std.alloc::alloc_error {
    const u8[] key_bytes = key;
    try {
        switch (move text) {
        case variant o::some(move value):
            std.json::insert(document, key_bytes, converted(property, name, value));
        case variant o::none:
            if (nullable(property) == true) {
                std.json::insert(document, key_bytes, std.json::null());
            } else {
                throw (required_by(described, key) == true) field_failure(error_code::missing_value, name);
            }
        }
    } catch (std.json::error rejected) {
        (move rejected) as void;
        throw field_failure(error_code::invalid_value, name);
    }
}

/* The name of the environment variable of a property: the prefix and the property in upper
   case, with every byte that is not an ASCII letter or digit written as `_`. */
protected std.string::string environment_name(str prefix, str key) throws std.alloc::alloc_error {
    std.string::string name = std.string::from_str(prefix);
    const u8[] bytes_of_key = key;
    for (usize index = 0usize; index < len(bytes_of_key); index += 1usize) {
        u8 byte = bytes_of_key[index];
        u8 upper = byte;
        if (byte >= 97u8 && byte <= 122u8) { upper = (byte - 32u8) as u8; }
        if ((upper < 65u8 || upper > 90u8) && (upper < 48u8 || upper > 57u8)) { upper = 95u8; }
        std.string::push_scalar(&name, upper as char);
    }
    return move name;
}

protected o<std.string::string> environment_value(str name) throws std.alloc::alloc_error {
    try {
        return std.env::get(name);
    } catch (std.env::env_error rejected) {
        rejected as void;
    }
    return o::none;
}

protected std.json::value empty_object() throws field_error, std.alloc::alloc_error {
    try {
        return std.json::object();
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw field_failure(error_code::invalid_value, "");
}

/* The value of T read with std.json::unmarshal from the document of its settings. */
@generic<T: json_decode>
protected T read_document(std.json::value document) throws field_error, std.alloc::alloc_error {
    try {
        std.string::string text = std.json::stringify(&document);
        T result = std.json::unmarshal(text);
        return move result;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw field_failure(error_code::invalid_value, "");
}

@generic<T: json_decode>
protected std.json::value schema_of() throws field_error, std.alloc::alloc_error {
    try {
        return std.json::schema::<T>();
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw field_failure(error_code::invalid_value, "");
}

/* R-SLIB-CONFIG-0004: a T whose fields come from the environment variables of their names. */
@generic<T: json_decode>
T from_environment(str prefix) throws field_error, std.alloc::alloc_error {
    std.json::value described = schema_of::<T>();
    std.json::value document = empty_object();
    switch (schema_member(&described, "properties")) {
    case variant o::some(properties):
        for (usize index = 0usize; index < std.json::len(*properties); index += 1usize) {
            str key = std.json::key_at(*properties, index);
            switch (std.json::get(*properties, index)) {
            case variant o::some(property):
                std.string::string name = environment_name(prefix, key);
                place(&document, &described, key, *property, name, environment_value(name));
            case variant o::none: key as void;
            }
        }
    case variant o::none: break;
    }
    return read_document::<T>(move document);
}

/* R-SLIB-CONFIG-0004: a T whose fields come from the keys of their names, after every layer. */
@generic<T: json_decode>
T config::decode(const config* this) throws field_error, std.alloc::alloc_error {
    std.json::value described = schema_of::<T>();
    std.json::value document = empty_object();
    switch (schema_member(&described, "properties")) {
    case variant o::some(properties):
        for (usize index = 0usize; index < std.json::len(*properties); index += 1usize) {
            str key = std.json::key_at(*properties, index);
            switch (std.json::get(*properties, index)) {
            case variant o::some(property):
                const u8[] key_bytes = key;
                o<std.string::string> value = o::none;
                switch (this->find(key_bytes)) {
                case variant o::some(at):
                    value = o::some(std.string::from_str(this->entries[*at].value));
                case variant o::none: break;
                }
                place(&document, &described, key, *property, key, move value);
            case variant o::none: key as void;
            }
        }
    case variant o::none: break;
    }
    return read_document::<T>(move document);
}
