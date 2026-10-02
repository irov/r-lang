module std.config;
import std.args;
import std.text;

/* R-SLIB-CONFIG-0001: where the current value of a key came from; later layers win. */
enum source { default_value, file, environment, arguments };

enum error_code {
    invalid_key,
    duplicate_key,
    unknown_key,
    invalid_value,
    invalid_file,
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
        throw (same(existing->key.as_str(), key) == true)
            config_error {.code = error_code::duplicate_key, .index = index};
    }
    append(&this->entries, entry {.key = std.string::from_str(key),
                                  .value = std.string::from_str(default_value),
                                  .help = std.string::from_str(help),
                                  .origin = source::default_value});
}

protected o<usize> config::find(const config* this, const u8[] key) {
    for (usize index = 0usize; index < len(this->entries); index += 1usize) {
        if (same(this->entries[index].key.as_str(), key) == true) { return o::some(index); }
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
                std.string::string key = std.string::from_str(item.prefix.as_str());
                if (std.string::len(&key) != 0usize) { key.append("."); }
                key.append(name->as_str());
                std.json::value member = std.json::take_field(&item.node, name->as_str());
                std.json::value_kind kind = std.json::kind(&member);
                if (kind == std.json::value_kind::object) {
                    append(&work, pending {.node = move member, .prefix = move key});
                } else {
                    this->collect(&updates, key.as_str(), &member);
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
    std.string::string joined = std.text::replace(upper.as_str(), ".", "_");
    return f"{prefix}_{joined}";
}

/* R-SLIB-CONFIG-0002: the variables `PREFIX_KEY` of the declared keys that are set. */
void config::load_environment(config* this, str prefix) throws std.error::fault {
    for (usize index = 0usize; index < len(this->entries); index += 1usize) {
        std.string::string name = variable_name(prefix, this->entries[index].key.as_str());
        o<std.string::string> value = std.env::get(name.as_str());
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
            str key = this->entries[index].key.as_str();
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
    return this->entries[index].value.as_str();
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
        return std.convert::parse_i64(this->entries[index].value.as_str(), 10u32);
    } catch (std.convert::parse_error failure) {
        failure as void;
    }
    throw config_error {.code = error_code::invalid_value, .index = index};
}

u64 config::get_u64(const config* this, str key) throws config_error {
    usize index = this->index_of(key);
    try {
        return std.convert::parse_u64(this->entries[index].value.as_str(), 10u32);
    } catch (std.convert::parse_error failure) {
        failure as void;
    }
    throw config_error {.code = error_code::invalid_value, .index = index};
}

/* R-SLIB-CONFIG-0003: `true` or `false`, anything else invalid_value. */
bool config::get_bool(const config* this, str key) throws config_error {
    usize index = this->index_of(key);
    str text = this->entries[index].value.as_str();
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
        if (sensitive(item->key.as_str()) == true) {
            std.string::string line = f"{item->key}=*** ({origin})\n";
            text.append(line.as_str());
        } else {
            std.string::string line = f"{item->key}={item->value} ({origin})\n";
            text.append(line.as_str());
        }
    }
    return move text;
}
