module std.regex;

/* Iterative Thompson NFA. Public positions are UTF-8 byte offsets. */
enum error_code {
    invalid_pattern,
    unsupported,
    program_limit,
    step_limit,
    invalid_options,
    invalid_offset,
};

error error {
    error_code code;
    usize offset;
};

struct options {
    bool ignore_ascii_case;
    bool multiline;
    bool dot_all;
    usize max_steps;
};

struct span {
    usize start;
    usize end;
};

/* Opcodes: 1 literal, 2 dot, 3 class, 4 split, 5 epsilon, 6 start, 7 end,
   8 ASCII word boundary, 9 accept, 10 save of a group boundary (value is the slot). Links and
   class bounds are indices, never pointers. */
protected struct instruction {
    i32 op;
    usize next;
    usize other;
    u32 value;
    usize limit;
    bool inverted;
};

protected struct scalar_range {
    u32 first;
    u32 last;
};

struct regex {
    protected array<instruction> code;
    protected array<scalar_range> ranges;
    protected usize entry;
    protected options settings;
    protected usize groups;
};

protected struct fragment {
    usize entry;
    usize exit;
    usize base;
};

protected struct parse_frame {
    fragment sequence;
    fragment alternative;
    bool has_sequence;
    bool has_alternative;
    usize offset;
    usize group;
};

protected struct thread {
    usize state;
    usize start;
};

/* Reserve propagates the exact allocation reason before insertion. An insertion into the
   reserved slot performs no allocation. Private rejected owners are destroyed by the catch. */
@generic<T: unborrowed>
protected void append(array<T>* target, T value) throws std.alloc::alloc_error {
    target->reserve(1usize);
    bool failed = false;
    try {
        target->push(move value);
    } catch (std.array::push_error<T> failure) {
        failed = true;
    }
    if (failed == true) { panic("reserved array insertion failed"); }
}

options default_options() {
    return options {
        .ignore_ascii_case = false,
        .multiline = false,
        .dot_all = false,
        .max_steps = 10000000usize,
    };
}

@generic<T: copy>
protected T read_at(const array<T>* source, usize index) {
    const T[] values = source->as_slice();
    return values[index];
}

@generic<T: copy & unborrowed>
protected void write_at(array<T>* target, usize index, T value) {
    T[] values = target->as_slice_mut();
    values[index] = value;
}

protected void set_next(regex* target, usize index, usize destination) {
    instruction[] values = std.array::as_slice_mut(&target->code);
    values[index].next = destination;
}

protected void set_other(regex* target, usize index, usize destination) {
    instruction[] values = std.array::as_slice_mut(&target->code);
    values[index].other = destination;
}

protected void set_value(regex* target, usize index, u32 value) {
    instruction[] values = std.array::as_slice_mut(&target->code);
    values[index].value = value;
}

protected void set_class(regex* target, usize index, usize begin, usize end, bool inverted) {
    instruction[] values = std.array::as_slice_mut(&target->code);
    values[index].other = begin;
    values[index].limit = end;
    values[index].inverted = inverted;
}

protected usize width(u8 first) {
    if (first < 128u8) { return 1usize; }
    if (first < 224u8) { return 2usize; }
    if (first < 240u8) { return 3usize; }
    return 4usize;
}

/* str has already been validated by the language boundary. */
protected u32 scalar(const u8[] text, usize at) {
    u32 a = text[at] as u32;
    if (a < 128u32) { return a; }
    usize count = width(text[at]);
    u32 value = a & 31u32;
    if (count == 3usize) { value = a & 15u32; }
    if (count == 4usize) { value = a & 7u32; }
    usize index = 1usize;
    while (index < count) {
        value = value * 64u32 + ((text[at + index] as u32) & 63u32);
        index += 1usize;
    }
    return value;
}

protected u32 ascii_lower(u32 value) {
    if (value >= 65u32 && value <= 90u32) { return value + 32u32; }
    return value;
}

protected bool word(u32 value) {
    return (value >= 65u32 && value <= 90u32) ||
        (value >= 97u32 && value <= 122u32) ||
        (value >= 48u32 && value <= 57u32) || value == 95u32;
}

protected usize emit(regex* target, i32 op, usize offset)
    throws error, std.alloc::alloc_error {
    usize count = len(target->code);
    throw (count >= 4096usize) error { .code = error_code::program_limit, .offset = offset };
    instruction node = instruction {
        .op = op, .next = 0usize, .other = 0usize, .value = 0u32,
        .limit = 0usize, .inverted = false,
    };
    append(&target->code, node);
    return count;
}

protected fragment atom(regex* target, i32 op, u32 value, usize offset)
    throws error, std.alloc::alloc_error {
    usize first = emit(target, op, offset);
    usize last = emit(target, 5, offset);
    set_next(target, first, last);
    set_value(target, first, value);
    return fragment { .entry = first, .exit = last, .base = first };
}

protected fragment concatenate(regex* target, fragment left, fragment right) {
    set_next(target, left.exit, right.entry);
    return fragment { .entry = left.entry, .exit = right.exit, .base = left.base };
}

protected fragment alternate(regex* target, fragment left, fragment right, usize offset)
    throws error, std.alloc::alloc_error {
    usize first = emit(target, 4, offset);
    usize last = emit(target, 5, offset);
    set_next(target, first, left.entry);
    set_other(target, first, right.entry);
    set_next(target, left.exit, last);
    set_next(target, right.exit, last);
    return fragment { .entry = first, .exit = last, .base = left.base };
}

protected fragment quantify(regex* target, fragment item, u8 operator, usize offset)
    throws error, std.alloc::alloc_error {
    usize first = emit(target, 4, offset);
    usize last = emit(target, 5, offset);
    set_next(target, first, item.entry);
    set_other(target, first, last);
    if (operator == 63u8) {
        set_next(target, item.exit, last);
    } else {
        set_next(target, item.exit, first);
    }
    usize entry = first;
    if (operator == 43u8) { entry = item.entry; }
    return fragment { .entry = entry, .exit = last, .base = item.base };
}

protected fragment copy_fragment(regex* target, fragment item, usize end, usize offset)
    throws error, std.alloc::alloc_error {
    usize base = len(target->code);
    usize index = item.base;
    while (index < end) {
        instruction copy = read_at(&target->code, index);
        usize added = emit(target, copy.op, offset);
        if (copy.next >= item.base && copy.next < end && index != item.exit) {
            copy.next = base + (copy.next - item.base);
        }
        if (copy.op == 4) { copy.other = base + (copy.other - item.base); }
        write_at(&target->code, added, copy);
        index += 1usize;
    }
    usize entry = base + (item.entry - item.base);
    usize exit = base + (item.exit - item.base);
    set_next(target, exit, 0usize);
    return fragment { .entry = entry, .exit = exit, .base = base };
}

protected usize decimal(const u8[] source, usize* at) throws error {
    usize begin = *at;
    usize value = 0usize;
    while (*at < len(source)) {
        u8 byte = source[*at];
        if (byte < 48u8 || byte > 57u8) { break; }
        value = value * 10usize + ((byte - 48u8) as usize);
        throw (value > 1000usize) error { .code = error_code::program_limit, .offset = begin };
        *at += 1usize;
    }
    throw (*at == begin) error { .code = error_code::invalid_pattern, .offset = begin };
    return value;
}

protected fragment counted(regex* target, fragment item, const u8[] source, usize* at)
    throws error, std.alloc::alloc_error {
    usize offset = *at;
    *at += 1usize;
    usize minimum = decimal(source, at);
    usize maximum = minimum;
    bool unlimited = false;
    if (*at < len(source) && source[*at] == 44u8) {
        *at += 1usize;
        if (*at < len(source) && source[*at] == 125u8) {
            unlimited = true;
        } else {
            maximum = decimal(source, at);
        }
    }
    throw (*at >= len(source) || source[*at] != 125u8 ||
           (unlimited == false && maximum < minimum))
        error { .code = error_code::invalid_pattern, .offset = offset };
    *at += 1usize;
    usize end = len(target->code);
    usize copies = maximum;
    if (unlimited == true) { copies = minimum + 1usize; }
    if (copies == 0usize) {
        fragment result = atom(target, 5, 0u32, offset);
        result.base = item.base;
        return result;
    }
    /* Clone every original atom before connecting or quantifying any copy. */
    array<fragment> pieces = std.array::create::<fragment>();
    append(&pieces, item);
    usize index = 1usize;
    while (index < copies) {
        fragment copy = copy_fragment(target, item, end, offset);
        append(&pieces, copy);
        index += 1usize;
    }
    fragment result = read_at(&pieces, 0usize);
    if (minimum == 0usize) {
        u8 operator = 63u8;
        if (unlimited == true) { operator = 42u8; }
        result = quantify(target, result, operator, offset);
    }
    usize index_2 = 1usize;
    while (index_2 < copies) {
        fragment part = read_at(&pieces, index_2);
        if (index_2 >= minimum) {
            u8 operator = 63u8;
            if (unlimited == true) { operator = 42u8; }
            part = quantify(target, part, operator, offset);
        }
        result = concatenate(target, result, part);
        index_2 += 1usize;
    }
    return result;
}

protected void add_range(regex* target, u32 first, u32 last, usize offset)
    throws error, std.alloc::alloc_error {
    throw (len(target->ranges) >= 4096usize)
        error { .code = error_code::program_limit, .offset = offset };
    scalar_range range = scalar_range { .first = first, .last = last };
    append(&target->ranges, range);
}

/* Escape decoding returns a scalar, an ASCII class (-1), or an assertion (-2). */
protected i32 escape(regex* target, const u8[] source, usize* at, u32* value, bool* inverted)
    throws error, std.alloc::alloc_error {
    usize offset = *at;
    *at += 1usize;
    throw (*at >= len(source)) error { .code = error_code::invalid_pattern, .offset = offset };
    u8 byte = source[*at];
    *at += 1usize;
    *inverted = false;
    if (byte == 100u8 || byte == 68u8 || byte == 119u8 || byte == 87u8 ||
        byte == 115u8 || byte == 83u8) {
        *inverted = byte == 68u8 || byte == 87u8 || byte == 83u8;
        if (byte == 100u8 || byte == 68u8 || byte == 119u8 || byte == 87u8) {
            add_range(target, 48u32, 57u32, offset);
        }
        if (byte == 119u8 || byte == 87u8) {
            add_range(target, 65u32, 90u32, offset);
            add_range(target, 97u32, 122u32, offset);
            add_range(target, 95u32, 95u32, offset);
        }
        if (byte == 115u8 || byte == 83u8) {
            add_range(target, 9u32, 13u32, offset);
            add_range(target, 32u32, 32u32, offset);
        }
        return -1;
    }
    if (byte == 98u8 || byte == 66u8) {
        *inverted = byte == 66u8;
        return -2;
    }
    if (byte == 110u8) { *value = 10u32; return 0; }
    if (byte == 114u8) { *value = 13u32; return 0; }
    if (byte == 116u8) { *value = 9u32; return 0; }
    if (byte == 102u8) { *value = 12u32; return 0; }
    if (byte == 118u8) { *value = 11u32; return 0; }
    if (byte == 120u8) {
        u32 result = 0u32;
        usize digits = 0usize;
        while (digits < 2usize) {
            throw (*at >= len(source)) error { .code = error_code::invalid_pattern, .offset = offset };
            u8 digit = source[*at];
            u32 part = 16u32;
            if (digit >= 48u8 && digit <= 57u8) { part = (digit - 48u8) as u32; }
            if (digit >= 65u8 && digit <= 70u8) { part = ((digit - 65u8) as u32) + 10u32; }
            if (digit >= 97u8 && digit <= 102u8) { part = ((digit - 97u8) as u32) + 10u32; }
            throw (part == 16u32) error { .code = error_code::invalid_pattern, .offset = *at };
            result = result * 16u32 + part;
            *at += 1usize;
            digits += 1usize;
        }
        *value = result;
        return 0;
    }
    throw ((byte >= 48u8 && byte <= 57u8) || (byte >= 65u8 && byte <= 90u8) ||
           (byte >= 97u8 && byte <= 122u8) || byte >= 128u8)
        error { .code = error_code::unsupported, .offset = offset };
    *value = byte as u32;
    return 0;
}

protected fragment character_class(regex* target, const u8[] source, usize* at)
    throws error, std.alloc::alloc_error {
    usize offset = *at;
    *at += 1usize;
    bool inverted = false;
    if (*at < len(source) && source[*at] == 94u8) { inverted = true; *at += 1usize; }
    usize begin = len(target->ranges);
    bool closed = false;
    while (*at < len(source)) {
        if (source[*at] == 93u8) { *at += 1usize; closed = true; break; }
        u32 first = 0u32;
        i32 kind = 0;
        bool negate = false;
        if (source[*at] == 92u8) {
            kind = escape(target, source, at, &first, &negate);
            throw (kind == -2 || negate == true)
                error { .code = error_code::unsupported, .offset = offset };
            if (kind == -1) { continue; }
        } else {
            throw (source[*at] == 45u8 && len(target->ranges) != begin &&
                   *at + 1usize < len(source) && source[*at + 1usize] != 93u8)
                error { .code = error_code::invalid_pattern, .offset = *at };
            first = scalar(source, *at);
            usize step = width(source[*at]);
            *at += step;
        }
        u32 last = first;
        if (*at + 1usize < len(source) && source[*at] == 45u8 && source[*at + 1usize] != 93u8) {
            *at += 1usize;
            if (source[*at] == 92u8) {
                kind = escape(target, source, at, &last, &negate);
                throw (kind != 0) error { .code = error_code::invalid_pattern, .offset = *at };
            } else {
                last = scalar(source, *at);
                usize step = width(source[*at]);
                *at += step;
            }
            throw (last < first) error { .code = error_code::invalid_pattern, .offset = offset };
        }
        add_range(target, first, last, offset);
    }
    throw (closed == false || len(target->ranges) == begin)
        error { .code = error_code::invalid_pattern, .offset = offset };
    fragment result = atom(target, 3, 0u32, offset);
    usize end = len(target->ranges);
    set_class(target, result.entry, begin, end, inverted);
    return result;
}

protected parse_frame empty_frame(usize offset) {
    fragment empty = fragment { .entry = 0usize, .exit = 0usize, .base = 0usize };
    return parse_frame {
        .sequence = empty, .alternative = empty, .has_sequence = false,
        .has_alternative = false, .offset = offset, .group = 0usize,
    };
}

protected fragment finish_sequence(regex* target, parse_frame frame, usize offset)
    throws error, std.alloc::alloc_error {
    fragment result = frame.sequence;
    if (frame.has_sequence == false) { result = atom(target, 5, 0u32, offset); }
    return result;
}

protected fragment finish_frame(regex* target, parse_frame frame, usize offset)
    throws error, std.alloc::alloc_error {
    fragment result = finish_sequence(target, frame, offset);
    if (frame.has_alternative == true) {
        result = alternate(target, frame.alternative, result, offset);
    }
    return result;
}

regex compile_with_options(str pattern, options settings) throws error, std.alloc::alloc_error {
    throw (settings.max_steps == 0usize)
        error { .code = error_code::invalid_options, .offset = 0usize };
    const u8[] source = pattern;
    throw (len(source) > 16384usize) error { .code = error_code::program_limit, .offset = 16384usize };
    array<instruction> code = std.array::create::<instruction>();
    array<scalar_range> ranges = std.array::create::<scalar_range>();
    regex result = regex { .code = move code, .ranges = move ranges, .entry = 0usize,
                           .settings = settings, .groups = 0usize };
    array<parse_frame> frames = std.array::create::<parse_frame>();
    parse_frame frame = empty_frame(0usize);
    usize at = 0usize;
    while (at < len(source)) {
        usize offset = at;
        u8 byte = source[at];
        if (byte == 40u8) {
            throw (len(frames) >= 128usize) error { .code = error_code::program_limit, .offset = at };
            append(&frames, frame);
            frame = empty_frame(at);
            at += 1usize;
            if (at < len(source) && source[at] == 63u8) {
                throw (at + 1usize >= len(source) || source[at + 1usize] != 58u8)
                    error { .code = error_code::unsupported, .offset = offset };
                at += 2usize;
            } else {
                /* R-SLIB-REGEX-0007: a group captures; groups count from 1 in the order of their
                   opening parentheses. */
                throw (result.groups >= 255usize)
                    error { .code = error_code::program_limit, .offset = offset };
                result.groups += 1usize;
                frame.group = result.groups;
            }
            continue;
        }
        if (byte == 124u8) {
            fragment left = finish_frame(&result, frame, at);
            frame.has_alternative = true;
            frame.alternative = left;
            frame.has_sequence = false;
            at += 1usize;
            continue;
        }
        fragment item = fragment { .entry = 0usize, .exit = 0usize, .base = 0usize };
        if (byte == 41u8) {
            throw (len(frames) == 0usize) error { .code = error_code::invalid_pattern, .offset = at };
            item = finish_frame(&result, frame, at);
            if (frame.group != 0usize) {
                /* The group's boundaries are saved around its fragment, after its instructions,
                   so that a counted repetition copies them with the group. */
                usize open = emit(&result, 10, frame.offset);
                usize close = emit(&result, 10, at);
                set_value(&result, open, ((frame.group - 1usize) * 2usize) as u32);
                set_value(&result, close, ((frame.group - 1usize) * 2usize + 1usize) as u32);
                set_next(&result, open, item.entry);
                set_next(&result, item.exit, close);
                item = fragment { .entry = open, .exit = close, .base = item.base };
            }
            usize frame_index = len(frames) - 1usize;
            frame = read_at(&frames, frame_index);
            o<parse_frame> removed = frames.pop();
            removed as void;
            at += 1usize;
        } else { if (byte == 91u8) {
            item = character_class(&result, source, &at);
        } else { if (byte == 92u8) {
            usize begin = len(result.ranges);
            u32 value = 0u32;
            bool inverted = false;
            i32 kind = escape(&result, source, &at, &value, &inverted);
            i32 op = 1;
            if (kind == -1) { op = 3; }
            if (kind == -2) { op = 8; }
            item = atom(&result, op, value, offset);
            usize end = len(result.ranges);
            set_class(&result, item.entry, begin, end, inverted);
        } else {
            throw (byte == 42u8 || byte == 43u8 || byte == 63u8 || byte == 123u8 || byte == 125u8 || byte == 93u8)
                error { .code = error_code::invalid_pattern, .offset = at };
            i32 op = 1;
            if (byte == 46u8) { op = 2; }
            if (byte == 94u8) { op = 6; }
            if (byte == 36u8) { op = 7; }
            u32 value = scalar(source, at);
            item = atom(&result, op, value, at);
            usize step = width(byte);
            at += step;
        }
        } }
        if (at < len(source)) {
            u8 operator = source[at];
            if (operator == 42u8 || operator == 43u8 || operator == 63u8 || operator == 123u8) {
                throw (byte == 94u8 || byte == 36u8)
                    error { .code = error_code::invalid_pattern, .offset = at };
                if (operator == 123u8) { item = counted(&result, item, source, &at); }
                else { item = quantify(&result, item, operator, at); at += 1usize; }
                throw (at < len(source) && (source[at] == 63u8 || source[at] == 43u8))
                    error { .code = error_code::unsupported, .offset = at };
            }
        }
        if (frame.has_sequence == true) { frame.sequence = concatenate(&result, frame.sequence, item); }
        else { frame.sequence = item; frame.has_sequence = true; }
    }
    throw (len(frames) != 0usize) error { .code = error_code::invalid_pattern, .offset = frame.offset };
    fragment complete = finish_frame(&result, frame, at);
    usize terminal = emit(&result, 9, at);
    set_next(&result, complete.exit, terminal);
    result.entry = complete.entry;
    return move result;
}

regex compile(str pattern) throws error, std.alloc::alloc_error {
    options settings = default_options();
    regex result = compile_with_options(pattern, settings);
    return move result;
}

protected void tick(usize* remaining, usize offset) throws error {
    throw (*remaining == 0usize) error { .code = error_code::step_limit, .offset = offset };
    *remaining -= 1usize;
}

protected bool assertion(instruction node, options settings, const u8[] source, usize position) {
    usize length = len(source);
    if (node.op == 6) {
        return position == 0usize ||
            (settings.multiline == true && source[position - 1usize] == 10u8);
    }
    if (node.op == 7) {
        return position == length ||
            (settings.multiline == true && source[position] == 10u8);
    }
    bool before = false;
    bool after = false;
    if (position != 0usize) { before = word(source[position - 1usize] as u32); }
    if (position != len(source)) { after = word(source[position] as u32); }
    bool boundary = before != after;
    return boundary != node.inverted;
}

protected void closure(const regex* compiled, thread seed, const u8[] source, usize position,
    array<usize>* seen, array<thread>* active, array<usize>* pending, usize* remaining)
    throws error, std.alloc::alloc_error {
    append(pending, seed.state);
    while (len(*pending) != 0usize) {
        usize pending_index = len(*pending) - 1usize;
        usize state = read_at(pending, pending_index);
        o<usize> removed = pending->pop();
        removed as void;
        tick(remaining, position);
        usize visited = read_at(seen, state);
        if (visited == position + 1usize) { continue; }
        write_at(seen, state, position + 1usize);
        instruction node = read_at(&compiled->code, state);
        if (node.op == 4) {
            append(pending, node.other);
            append(pending, node.next);
        } else { if (node.op == 5 || node.op == 10) {
            append(pending, node.next);
        } else { if (node.op == 6 || node.op == 7 || node.op == 8) {
            bool matches = assertion(node, compiled->settings, source, position);
            if (matches == true) { append(pending, node.next); }
        } else {
            thread ready = thread { .state = state, .start = seed.start };
            append(active, ready);
        }
        } }
    }
}

protected bool accepts(const regex* compiled, instruction node, u32 value, usize* remaining, usize offset)
    throws error {
    tick(remaining, offset);
    if (node.op == 2) { return value != 10u32 || compiled->settings.dot_all == true; }
    bool fold = compiled->settings.ignore_ascii_case;
    if (node.op == 1) {
        u32 expected = node.value;
        if (fold == true) { value = ascii_lower(value); expected = ascii_lower(expected); }
        return value == expected;
    }
    if (node.op != 3) { return false; }
    bool found = false;
    usize index = node.other;
    while (index < node.limit) {
        tick(remaining, offset);
        scalar_range range = read_at(&compiled->ranges, index);
        if (value >= range.first && value <= range.last) { found = true; }
        if (fold == true) {
            u32 lower = ascii_lower(value);
            if (lower >= 97u32 && lower <= 122u32) {
                u32 upper = lower - 32u32;
                if ((lower >= range.first && lower <= range.last) ||
                    (upper >= range.first && upper <= range.last)) { found = true; }
            }
        }
        index += 1usize;
    }
    return found != node.inverted;
}

protected struct SearchWorkspace {
    array<thread> seeds;
    array<thread> active;
    array<usize> pending;
    array<usize> seen;
    u32 scalar;
};

protected o<span> search(const regex* compiled, str text, usize offset, bool whole, usize* remaining)
    throws error, std.alloc::alloc_error {
    const u8[] source = text;
    throw (offset > len(source)) error { .code = error_code::invalid_offset, .offset = offset };
    throw (offset < len(source) && (source[offset] & 192u8) == 128u8)
        error { .code = error_code::invalid_offset, .offset = offset };
    usize count = len(compiled->code);
    SearchWorkspace workspace = {
        .seeds = std.array::with_capacity::<thread>(count),
        .active = std.array::with_capacity::<thread>(count),
        .pending = std.array::with_capacity::<usize>(count * 2usize + 1usize),
        .seen = std.array::with_capacity::<usize>(count),
        .scalar = 0u32,
    };
    usize index = 0usize;
    while (index < count) { append(&workspace.seen, 0usize); index += 1usize; }
    bool found = false;
    span best = span { .start = 0usize, .end = 0usize };
    usize position = offset;
    while (position <= len(source)) {
        workspace.active.clear();
        index = 0usize;
        while (index < len(workspace.seeds)) {
            thread seed = read_at(&workspace.seeds, index);
            closure(compiled, seed, source, position, &workspace.seen, &workspace.active, &workspace.pending, remaining);
            index += 1usize;
        }
        if (found == false && (whole == false || position == 0usize)) {
            thread seed = thread { .state = compiled->entry, .start = position };
            closure(compiled, seed, source, position, &workspace.seen, &workspace.active, &workspace.pending, remaining);
        }
        workspace.seeds.clear();
        if (position < len(source)) { workspace.scalar = scalar(source, position); }
        index = 0usize;
        while (index < len(workspace.active)) {
            thread current = read_at(&workspace.active, index);
            instruction node = read_at(&compiled->code, current.state);
            if (node.op == 9) {
                if ((whole == false || position == len(source)) &&
                    (found == false || current.start <= best.start)) {
                    best.start = current.start;
                    best.end = position;
                    found = true;
                }
            } else { if (position < len(source) && (found == false || current.start <= best.start)) {
                bool matches = accepts(compiled, node, workspace.scalar, remaining, position);
                if (matches == true) {
                    thread next = thread { .state = node.next, .start = current.start };
                    append(&workspace.seeds, next);
                }
            }
            }
            index += 1usize;
        }
        if (position == len(source) || (len(workspace.seeds) == 0usize && (found == true || whole == true))) { break; }
        usize step = width(source[position]);
        position += step;
    }
    if (found == true) { return o::some(best); }
    return o::none;
}

o<span> find_from(const regex* compiled, str text, usize offset) throws error, std.alloc::alloc_error {
    usize remaining = compiled->settings.max_steps;
    o<span> result = search(compiled, text, offset, false, &remaining);
    return result;
}

o<span> find(const regex* compiled, str text) throws error, std.alloc::alloc_error {
    o<span> result = find_from(compiled, text, 0usize);
    return result;
}

bool is_match(const regex* compiled, str text) throws error, std.alloc::alloc_error {
    o<span> result = find(compiled, text);
    switch (result) {
    case variant o::some(found): return true;
    case variant o::none: return false;
    }
}

bool full_match(const regex* compiled, str text) throws error, std.alloc::alloc_error {
    usize remaining = compiled->settings.max_steps;
    o<span> result = search(compiled, text, 0usize, true, &remaining);
    switch (result) {
    case variant o::some(found): return true;
    case variant o::none: return false;
    }
}

protected array<span> collect(const regex* compiled, str text) throws error, std.alloc::alloc_error {
    array<span> matches = std.array::create::<span>();
    const u8[] source = text;
    usize at = 0usize;
    usize remaining = compiled->settings.max_steps;
    while (at <= len(source)) {
        o<span> found = search(compiled, text, at, false, &remaining);
        bool done = false;
        switch (found) {
        case variant o::some(value):
            append(&matches, *value);
            at = value->end;
            if (value->start == value->end) {
                if (at == len(source)) { done = true; }
                else { usize step = width(source[at]); at += step; }
            }
            break;
        case variant o::none:
            done = true;
            break;
        }
        if (done == true) { break; }
    }
    return move matches;
}

array<span> find_all(const regex* compiled, str text) throws error, std.alloc::alloc_error {
    array<span> result = collect(compiled, text);
    return move result;
}

protected void append_range(std.string::string* result, str text, usize begin, usize end)
    throws std.alloc::alloc_error {
    const u8[] source = text;
    usize index = begin;
    while (index < end) {
        u32 value = scalar(source, index);
        std.string::push_scalar(result, value as char);
        usize step = width(source[index]);
        index += step;
    }
}

std.string::string replace_all(const regex* compiled, str text, str replacement)
    throws error, std.alloc::alloc_error {
    array<span> matches = collect(compiled, text);
    std.string::string result = std.string::create();
    usize at = 0usize;
    for (const span* matched in &matches) {
        append_range(&result, text, at, matched->start);
        result.append(replacement);
        at = matched->end;
    }
    usize length = len(text);
    append_range(&result, text, at, length);
    return move result;
}

array<std.string::string> split(const regex* compiled, str text) throws error, std.alloc::alloc_error {
    array<span> matches = collect(compiled, text);
    array<std.string::string> result = std.array::create::<std.string::string>();
    usize at = 0usize;
    for (const span* matched in &matches) {
        std.string::string part = std.string::create();
        append_range(&part, text, at, matched->start);
        append(&result, move part);
        at = matched->end;
    }
    std.string::string tail = std.string::create();
    usize length = len(text);
    append_range(&tail, text, at, length);
    append(&result, move tail);
    return move result;
}

std.string::string escape_literal(str text) throws std.alloc::alloc_error {
    std.string::string result = std.string::create();
    const u8[] source = text;
    usize at = 0usize;
    while (at < len(source)) {
        u32 value = scalar(source, at);
        if (value == 92u32 || value == 46u32 || value == 94u32 || value == 36u32 ||
            value == 124u32 || value == 40u32 || value == 41u32 || value == 91u32 ||
            value == 93u32 || value == 123u32 || value == 125u32 || value == 42u32 ||
            value == 43u32 || value == 63u32) {
            result.append("\\");
        }
        result.append(value as char);
        usize step = width(source[at]);
        at += step;
    }
    return move result;
}

/* R-SLIB-REGEX-0007: the number of capturing groups. */
usize group_count(const regex* compiled) {
    return compiled->groups;
}

/* R-SLIB-REGEX-0007: a thread of the capture pass: its state and the offset of its block of
   slots, two per group, each a position plus one or zero when unset. */
protected struct capture_thread {
    usize state;
    usize block;
};

/* Adds the threads that `state` reaches at `position` without consuming, in priority order:
   the preferred branch of a split first, a save copying the block before it sets its slot. A
   state reached before at this position keeps the earlier, preferred thread. */
protected void capture_closure(const regex* compiled, usize state, usize block,
    const u8[] source, usize position, array<usize>* slots, array<usize>* seen,
    array<capture_thread>* ready, array<capture_thread>* pending, usize* remaining)
    throws error, std.alloc::alloc_error {
    append(pending, capture_thread { .state = state, .block = block });
    usize width_of_block = compiled->groups * 2usize;
    while (len(*pending) != 0usize) {
        usize top = len(*pending) - 1usize;
        capture_thread current = read_at(pending, top);
        o<capture_thread> removed = pending->pop();
        removed as void;
        tick(remaining, position);
        if (read_at(seen, current.state) == position + 1usize) { continue; }
        write_at(seen, current.state, position + 1usize);
        instruction node = read_at(&compiled->code, current.state);
        if (node.op == 4) {
            append(pending, capture_thread { .state = node.other, .block = current.block });
            append(pending, capture_thread { .state = node.next, .block = current.block });
        } else { if (node.op == 5) {
            append(pending, capture_thread { .state = node.next, .block = current.block });
        } else { if (node.op == 10) {
            usize copy = len(*slots);
            for (usize index = 0usize; index < width_of_block; index += 1usize) {
                usize saved = read_at(slots, current.block + index);
                append(slots, saved);
            }
            write_at(slots, copy + (node.value as usize), position + 1usize);
            append(pending, capture_thread { .state = node.next, .block = copy });
        } else { if (node.op == 6 || node.op == 7 || node.op == 8) {
            bool matches = assertion(node, compiled->settings, source, position);
            if (matches == true) {
                append(pending, capture_thread { .state = node.next, .block = current.block });
            }
        } else {
            append(ready, current);
        }
        } } }
    }
}

/* R-SLIB-REGEX-0007: the groups of the match source[start..end] that search selected: the
   preferred path among the paths that match exactly that span decides every group. */
protected struct CaptureWorkspace {
    array<usize> seen;
    array<usize> slots;
    array<usize> next_slots;
    array<capture_thread> threads;
    array<capture_thread> advanced;
    array<capture_thread> pending;
    u32 scalar;
};

protected array<o<span>> capture_groups(const regex* compiled, const u8[] source, usize start,
    usize end, usize* remaining) throws error, std.alloc::alloc_error {
    usize count = len(compiled->code);
    usize width_of_block = compiled->groups * 2usize;
    CaptureWorkspace workspace = {
        .seen = std.array::with_capacity::<usize>(count),
        .slots = std.array::with_capacity::<usize>(width_of_block),
        .next_slots = std.array::create::<usize>(),
        .threads = std.array::create::<capture_thread>(),
        .advanced = std.array::create::<capture_thread>(),
        .pending = std.array::create::<capture_thread>(),
        .scalar = 0u32,
    };
    for (usize index = 0usize; index < count; index += 1usize) {
        append(&workspace.seen, 0usize);
    }
    for (usize index = 0usize; index < width_of_block; index += 1usize) {
        append(&workspace.slots, 0usize);
    }
    capture_closure(compiled, compiled->entry, 0usize, source, start, &workspace.slots,
                    &workspace.seen, &workspace.threads, &workspace.pending, remaining);
    usize position = start;
    o<usize> chosen = o::none;
    while (true) {
        if (position == end) {
            for (usize index = 0usize; index < len(workspace.threads); index += 1usize) {
                capture_thread current = read_at(&workspace.threads, index);
                instruction node = read_at(&compiled->code, current.state);
                if (node.op == 9) {
                    chosen = o::some(current.block);
                    break;
                }
            }
            break;
        }
        workspace.scalar = scalar(source, position);
        usize step = width(source[position]);
        workspace.advanced.clear();
        workspace.next_slots.clear();
        for (usize index = 0usize; index < len(workspace.threads); index += 1usize) {
            capture_thread current = read_at(&workspace.threads, index);
            instruction node = read_at(&compiled->code, current.state);
            if (node.op == 9) { continue; }
            bool matches = accepts(compiled, node, workspace.scalar, remaining, position);
            if (matches == true) {
                usize block = len(workspace.next_slots);
                for (usize slot = 0usize; slot < width_of_block; slot += 1usize) {
                    usize saved = read_at(&workspace.slots, current.block + slot);
                    append(&workspace.next_slots, saved);
                }
                capture_closure(compiled, node.next, block, source, position + step,
                                &workspace.next_slots, &workspace.seen, &workspace.advanced,
                                &workspace.pending, remaining);
            }
        }
        workspace.threads =
            core::replace(&workspace.advanced, std.array::create::<capture_thread>());
        workspace.slots = core::replace(&workspace.next_slots, std.array::create::<usize>());
        position += step;
    }
    array<o<span>> groups = std.array::with_capacity::<o<span>>(compiled->groups + 1usize);
    o<span> whole = o::some(span { .start = start, .end = end });
    append(&groups, whole);
    switch (chosen) {
    case variant o::some(block):
        for (usize group = 0usize; group < compiled->groups; group += 1usize) {
            usize first = read_at(&workspace.slots, *block + group * 2usize);
            usize last = read_at(&workspace.slots, *block + group * 2usize + 1usize);
            bool present = last != 0usize;
            if (first == 0usize) { present = false; }
            if (present == true) {
                o<span> entry = o::some(span { .start = first - 1usize, .end = last - 1usize });
                append(&groups, entry);
            } else {
                o<span> entry = o::none;
                append(&groups, entry);
            }
        }
    case variant o::none:
        for (usize group = 0usize; group < compiled->groups; group += 1usize) {
            o<span> entry = o::none;
            append(&groups, entry);
        }
    }
    return move groups;
}

/* R-SLIB-REGEX-0007: the match that find_from selects with its groups: element zero is the
   match, element i the last span group i matched on the preferred path, none for a group
   that took no part; none when nothing matches. */
o<array<o<span>>> captures_from(const regex* compiled, str text, usize offset)
    throws error, std.alloc::alloc_error {
    usize remaining = compiled->settings.max_steps;
    o<span> found = search(compiled, text, offset, false, &remaining);
    const u8[] source = text;
    switch (found) {
    case variant o::some(value):
        array<o<span>> groups =
            capture_groups(compiled, source, value->start, value->end, &remaining);
        return o::some(move groups);
    case variant o::none:
        return o::none;
    }
}

o<array<o<span>>> captures(const regex* compiled, str text)
    throws error, std.alloc::alloc_error {
    return captures_from(compiled, text, 0usize);
}
