module std.args;

/* R-SLIB-ARGS-0001: what a declared name takes: nothing, a value, or a place among the
   positional arguments. */
protected enum kind { flag, option, positional };

enum error_code {
    invalid_name,
    duplicate_name,
    unknown_option,
    missing_value,
    unexpected_value,
    missing_argument,
    extra_argument,
    unknown_name,
};

/* R-SLIB-ARGS-0001..0003: `index` is the offending argument of a parse, the declaration of a
   declaration and zero for a query of an undeclared name. */
error args_error {
    error_code code;
    usize index;
};

/* One declaration: a long name, an optional one-letter short name, and its help. */
protected struct spec {
    std.string::string name;
    std.string::string short_name;
    std.string::string value_name;
    std.string::string help;
    kind kind;
    bool required;
};

/* R-SLIB-ARGS-0001: the declared flags, options and positional arguments of a program. */
struct parser {
    protected std.string::string program;
    protected std.string::string summary;
    protected array<spec> specs;
    protected std.string::string remaining_name;
    protected std.string::string remaining_help;
    protected bool takes_remaining;
};

/* One declared name after a parse: whether it was given, whether it takes a value, and its
   last value. */
protected struct slot {
    bool present;
    bool takes_value;
    std.string::string value;
};

/* R-SLIB-ARGS-0002: the arguments as the parser understood them. */
struct matches {
    protected array<std.string::string> names;
    protected array<slot> slots;
    protected array<std.string::string> extra;
    protected bool help;
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

/* The bytes of a UTF-8 argument cut at ASCII bytes, as an owned string. */
protected std.string::string text_of(const u8[] bytes) throws std.alloc::alloc_error {
    try {
        str text = std.utf8::validate(bytes);
        return std.string::from_str(text);
    } catch (core::utf8_error failure) {
        failure as void;
        return std.string::create();
    }
}

/* Spaces after the text up to `width` bytes. */
protected void pad(std.string::string* text, usize width) throws std.alloc::alloc_error {
    while (std.string::len(text) < width) { text->append(" "); }
}

protected bool same(const u8[] left, const u8[] right) {
    return std.bytes::equal(left, right);
}

protected bool name_character(u8 byte) {
    return (byte >= 97u8 && byte <= 122u8) || (byte >= 48u8 && byte <= 57u8) || byte == 45u8 ||
           byte == 95u8 || byte == 46u8;
}

/* A long name is 1..64 lowercase ASCII letters, digits, `-`, `_` or `.`, starting with a
   letter; a short name is empty or one ASCII letter. */
protected bool valid_name(const u8[] bytes) {
    if (len(bytes) == 0usize || len(bytes) > 64usize) { return false; }
    if (bytes[0] < 97u8 || bytes[0] > 122u8) { return false; }
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        if (name_character(bytes[index]) == false) { return false; }
    }
    return true;
}

protected bool valid_short(const u8[] bytes) {
    if (len(bytes) == 0usize) { return true; }
    if (len(bytes) != 1usize) { return false; }
    u8 byte = bytes[0];
    return (byte >= 97u8 && byte <= 122u8) || (byte >= 65u8 && byte <= 90u8);
}

/* R-SLIB-ARGS-0001: a parser for the program name and summary its help starts with. */
parser parser::create(str program, str summary) throws std.alloc::alloc_error {
    return parser {.program = std.string::from_str(program),
                   .summary = std.string::from_str(summary), .specs = std.array::create(),
                   .remaining_name = std.string::create(), .remaining_help = std.string::create(),
                   .takes_remaining = false};
}

protected void parser::declare(parser* this, str name, str short_name, str value_name,
                               str help, kind kind, bool required)
    throws args_error, std.alloc::alloc_error {
    usize index = len(this->specs);
    throw (valid_name(name) == false || valid_short(short_name) == false ||
           same(name, "help") == true || same(short_name, "h") == true)
        args_error {.code = error_code::invalid_name, .index = index};
    for (const spec* existing in &this->specs) {
        throw (same(existing->name, name) == true ||
               (len(short_name) != 0usize && same(existing->short_name, short_name) == true))
            args_error {.code = error_code::duplicate_name, .index = index};
    }
    append(&this->specs, spec {.name = std.string::from_str(name),
                               .short_name = std.string::from_str(short_name),
                               .value_name = std.string::from_str(value_name),
                               .help = std.string::from_str(help), .kind = kind,
                               .required = required});
}

/* R-SLIB-ARGS-0001: `--name` or `-s`, true when given. */
void parser::flag(parser* this, str name, str short_name, str help)
    throws args_error, std.alloc::alloc_error {
    this->declare(name, short_name, "", help, kind::flag, false);
}

/* R-SLIB-ARGS-0001: `--name value`, `--name=value`, `-s value` or `-svalue`; the last one given
   counts. */
void parser::option(parser* this, str name, str short_name, str value_name, str help)
    throws args_error, std.alloc::alloc_error {
    this->declare(name, short_name, value_name, help, kind::option, false);
}

/* R-SLIB-ARGS-0001: the next positional argument; required ones come before optional ones. */
void parser::positional(parser* this, str name, str help, bool required)
    throws args_error, std.alloc::alloc_error {
    usize index = len(this->specs);
    for (const spec* existing in &this->specs) {
        throw (existing->kind == kind::positional && existing->required == false &&
               required == true)
            args_error {.code = error_code::invalid_name, .index = index};
    }
    this->declare(name, "", "", help, kind::positional, required);
}

/* R-SLIB-ARGS-0001: every positional argument after the declared ones, in order. */
void parser::remaining(parser* this, str name, str help)
    throws args_error, std.alloc::alloc_error {
    throw (valid_name(name) == false || same(name, "help") == true ||
           this->takes_remaining == true)
        args_error {.code = error_code::invalid_name, .index = len(this->specs)};
    this->remaining_name = std.string::from_str(name);
    this->remaining_help = std.string::from_str(help);
    this->takes_remaining = true;
}

/* R-SLIB-ARGS-0003: the usage line and one line per declaration. */
std.string::string parser::help(const parser* this) throws std.alloc::alloc_error {
    std.string::string text = f"{this->program} - {this->summary}\nusage: {this->program} [options]";
    for (const spec* entry in &this->specs) {
        if (entry->kind == kind::positional) {
            if (entry->required == true) {
                std.string::string word = f" {entry->name}";
                text.append(word);
            } else {
                std.string::string word = f" [{entry->name}]";
                text.append(word);
            }
        }
    }
    if (this->takes_remaining == true) {
        std.string::string word = f" [{this->remaining_name}...]";
        text.append(word);
    }
    text.append("\n");
    for (const spec* entry in &this->specs) {
        std.string::string left = std.string::create();
        if (entry->kind == kind::positional) {
            left.append(entry->name);
        } else {
            if (std.string::len(&entry->short_name) != 0usize) {
                std.string::string short_form = f"-{entry->short_name}, ";
                left.append(short_form);
            }
            std.string::string long_form = f"--{entry->name}";
            left.append(long_form);
            if (entry->kind == kind::option) {
                std.string::string value = f" {entry->value_name}";
                left.append(value);
            }
        }
        text.append("  ");
        pad(&left, 24usize);
        text.append(left);
        std.string::string line = f" {entry->help}\n";
        text.append(line);
    }
    if (this->takes_remaining == true) {
        std.string::string left = std.string::from_str(this->remaining_name);
        pad(&left, 24usize);
        std.string::string line = f"  {left} {this->remaining_help}\n";
        text.append(line);
    }
    text.append("  -h, --help               print this help\n");
    return move text;
}

protected o<usize> parser::find_long(const parser* this, const u8[] name) {
    for (usize index = 0usize; index < len(this->specs); index += 1usize) {
        const spec* entry = &this->specs[index];
        if (entry->kind != kind::positional && same(entry->name, name) == true) {
            return o::some(index);
        }
    }
    return o::none;
}

protected o<usize> parser::find_short(const parser* this, u8 letter) {
    for (usize index = 0usize; index < len(this->specs); index += 1usize) {
        const u8[] short_name = this->specs[index].short_name;
        if (len(short_name) == 1usize && short_name[0] == letter) { return o::some(index); }
    }
    return o::none;
}

/* The declaration of the positional argument at `position` among the positional ones. */
protected o<usize> parser::find_positional(const parser* this, usize position) {
    usize seen = 0usize;
    for (usize index = 0usize; index < len(this->specs); index += 1usize) {
        if (this->specs[index].kind == kind::positional) {
            if (seen == position) { return o::some(index); }
            seen += 1usize;
        }
    }
    return o::none;
}

protected void matches::set(matches* this, usize index, const u8[] value)
    throws std.alloc::alloc_error {
    this->slots[index].present = true;
    this->slots[index].value = text_of(value);
}

/* R-SLIB-ARGS-0002: parses the arguments after the program name. `--` ends the options; `-h`
   and `--help` ask for help, and then required positional arguments may be missing. */
matches parser::parse(const parser* this, const str[] arguments)
    throws args_error, std.alloc::alloc_error {
    matches result = {.names = std.array::create(), .slots = std.array::create(),
                      .extra = std.array::create(), .help = false};
    for (const spec* entry in &this->specs) {
        append(&result.names, std.string::from_str(entry->name));
        append(&result.slots, slot {.present = false, .takes_value = entry->kind != kind::flag,
                                    .value = std.string::create()});
    }
    bool options_done = false;
    usize positional_index = 0usize;
    usize index = 1usize;
    while (index < len(arguments)) {
        const u8[] bytes = arguments[index];
        bool option = options_done == false && len(bytes) >= 2usize && bytes[0] == 45u8;
        if (option == true && same(bytes, "--") == true) {
            options_done = true;
        } else {
            if (option == true && (same(bytes, "-h") == true || same(bytes, "--help") == true)) {
                result.help = true;
            } else {
                if (option == true && bytes[1] == 45u8) {
                    index = this->parse_long(&result, arguments, index);
                } else {
                    if (option == true) {
                        index = this->parse_short(&result, arguments, index);
                    } else {
                        o<usize> target = this->find_positional(positional_index);
                        switch (target) {
                        case variant o::some(position): result.set(*position, bytes);
                        case variant o::none:
                            throw (this->takes_remaining == false)
                                args_error {.code = error_code::extra_argument, .index = index};
                            append::<std.string::string>(&result.extra, text_of(bytes));
                        }
                        positional_index += 1usize;
                    }
                }
            }
        }
        index += 1usize;
    }
    if (result.help == false) {
        for (usize position = 0usize; position < len(this->specs); position += 1usize) {
            throw (this->specs[position].kind == kind::positional &&
                   this->specs[position].required == true &&
                   result.slots[position].present == false)
                args_error {.code = error_code::missing_argument, .index = len(arguments)};
        }
    }
    return move result;
}

/* `--name` or `--name=value`; returns the index of the last argument it used. */
protected usize parser::parse_long(const parser* this, matches* result, const str[] arguments,
                                   usize index) throws args_error, std.alloc::alloc_error {
    const u8[] bytes = arguments[index];
    usize equals = len(bytes);
    for (usize at = len(bytes); at > 2usize; at -= 1usize) {
        if (bytes[at - 1usize] == 61u8) { equals = at - 1usize; }
    }
    o<usize> found = this->find_long(bytes[2usize..equals]);
    switch (found) {
    case variant o::none: throw args_error {.code = error_code::unknown_option, .index = index};
    case variant o::some(position):
        if (this->specs[*position].kind == kind::flag) {
            throw (equals != len(bytes))
                args_error {.code = error_code::unexpected_value, .index = index};
            result->slots[*position].present = true;
            return index;
        }
        if (equals != len(bytes)) {
            result->set(*position, bytes[equals + 1usize..len(bytes)]);
            return index;
        }
        throw (index + 1usize >= len(arguments))
            args_error {.code = error_code::missing_value, .index = index};
        result->set(*position, arguments[index + 1usize]);
        return index + 1usize;
    }
}

/* `-abc`: flags, and an option that takes the rest of the argument or the next one; returns
   the index of the last argument it used. */
protected usize parser::parse_short(const parser* this, matches* result, const str[] arguments,
                                    usize index) throws args_error, std.alloc::alloc_error {
    const u8[] bytes = arguments[index];
    for (usize at = 1usize; at < len(bytes); at += 1usize) {
        o<usize> found = this->find_short(bytes[at]);
        switch (found) {
        case variant o::none: throw args_error {.code = error_code::unknown_option, .index = index};
        case variant o::some(position):
            if (this->specs[*position].kind == kind::option) {
                if (at + 1usize < len(bytes)) {
                    result->set(*position, bytes[at + 1usize..len(bytes)]);
                    return index;
                }
                throw (index + 1usize >= len(arguments))
                    args_error {.code = error_code::missing_value, .index = index};
                result->set(*position, arguments[index + 1usize]);
                return index + 1usize;
            }
            result->slots[*position].present = true;
        }
    }
    return index;
}

protected usize matches::slot_of(const matches* this, str name) throws args_error {
    for (usize index = 0usize; index < len(this->names); index += 1usize) {
        if (same(this->names[index], name) == true) { return index; }
    }
    throw args_error {.code = error_code::unknown_name, .index = 0usize};
}

/* R-SLIB-ARGS-0003: whether `-h` or `--help` was given. */
bool matches::help_requested(const matches* this) {
    return this->help;
}

/* R-SLIB-ARGS-0003: whether the declared flag, option or positional argument was given. */
bool matches::has(const matches* this, str name) throws args_error {
    usize index = this->slot_of(name);
    return this->slots[index].present;
}

/* R-SLIB-ARGS-0003: the value of an option or positional argument that was given, or none;
   a flag has none. */
o<std.string::string> matches::value(const matches* this, str name)
    throws args_error, std.alloc::alloc_error {
    usize index = this->slot_of(name);
    if (this->slots[index].present == false || this->slots[index].takes_value == false) {
        return o::none;
    }
    return o::some(std.string::from_str(this->slots[index].value));
}

/* R-SLIB-ARGS-0003: the positional arguments after the declared ones. */
array<std.string::string> matches::remaining(const matches* this) throws std.alloc::alloc_error {
    array<std.string::string> copies = std.array::create();
    for (const std.string::string* item in &this->extra) {
        append(&copies, std.string::from_str(*item));
    }
    return move copies;
}
