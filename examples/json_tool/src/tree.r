module example.json_tool.tree;

std.json::options options(u32 indent, bool strict, std.json::mode mode) {
    std.json::options result = { .max_depth = 128usize, .max_value_bytes = 1048576usize,
        .indent = indent, .reject_unknown_fields = strict, .ignore_case = false, .mode = mode };
    return result;
}

std.string::string keys(const std.json::value* object) throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::array();
    usize count = object->len();
    for (usize index = 0usize; index < count; index += 1usize) {
        str key = object->key_at(index);
        std.json::value name = std.json::from_string(key);
        result.append(move name);
    }
    return result.stringify();
}

std.string::string field(const std.json::value* object, str key) throws std.json::error, std.alloc::alloc_error {
    o<const std.json::value*> found = object->find(key);
    switch (found) {
    case variant o::some(move value): return value->stringify();
    case variant o::none:
        std.json::value missing = std.json::null();
        return missing.stringify();
    }
}

// Make both ownership transfers observable: the extracted value and the remaining tree.
std.string::string extraction(std.json::value remaining, std.json::value taken)
    throws std.json::error, std.alloc::alloc_error {
    std.json::value result = std.json::object();
    result.insert("taken", move taken);
    result.insert("remaining", move remaining);
    return result.stringify();
}

struct Stats { usize nodes; usize nulls; usize booleans; usize enabled; usize numbers; usize strings; usize text_bytes; usize arrays; usize objects; };

std.string::string statistics(std.json::value root)
    throws std.json::error, std.alloc::alloc_error, std.array::push_error<std.json::value> {
    Stats total = { .nodes = 0usize, .nulls = 0usize, .booleans = 0usize, .enabled = 0usize,
        .numbers = 0usize, .strings = 0usize, .text_bytes = 0usize, .arrays = 0usize, .objects = 0usize };
    array<std.json::value> pending = std.array::create::<std.json::value>();
    pending.push(move root);
    while (len(pending) != 0usize) {
        o<std.json::value> next = pending.pop();
        switch (move next) {
        case variant o::some(move node):
            total.nodes += 1usize;
            std.json::value_kind kind = node.kind();
            if (kind == std.json::value_kind::null) { total.nulls += 1usize; break; }
            if (kind == std.json::value_kind::boolean) {
                total.booleans += 1usize;
                if (node.boolean() == true) { total.enabled += 1usize; }
                break;
            }
            if (kind == std.json::value_kind::number) { total.numbers += 1usize; break; }
            if (kind == std.json::value_kind::string) {
                total.strings += 1usize;
                str text = node.text();
                total.text_bytes += len(text); break;
            }
            if (kind == std.json::value_kind::array) { total.arrays += 1usize; }
            else { total.objects += 1usize; }
            while (node.len() != 0usize) {
                usize last = node.len() - 1usize;
                if (kind == std.json::value_kind::array) {
                    std.json::value child = node.take_index(last);
                    pending.push(move child);
                } else {
                    std.string::string key = std.string::from_str(node.key_at(last));
                    str name = key.as_str();
                    std.json::value child = node.take_field(name);
                    pending.push(move child);
                }
            }
            break;
        case variant o::none: break;
        }
    }
    return std.json::marshal(&total);
}

std.string::string element(const std.json::value* source, usize position)
    throws std.json::error, std.alloc::alloc_error {
    o<const std.json::value*> found = source->get(position);
    switch (found) {
    case variant o::some(move value): return value->stringify();
    case variant o::none:
        std.json::value missing = std.json::null();
        return missing.stringify();
    }
}

std.string::string number(str text) throws std.json::error, std.alloc::alloc_error {
    std.json::number exact = std.json::parse_number(text);
    str spelling = exact.text();
    bool zero = exact.is_zero();
    std.json::value report = std.json::object();
    std.json::value source = std.json::from_string(spelling);
    report.insert("spelling", move source);
    std.json::value flag = std.json::from_bool(zero);
    report.insert("zero", move flag);
    std.json::value value = std.json::from_number(&exact);
    report.insert("value", move value);
    return report.stringify();
}
