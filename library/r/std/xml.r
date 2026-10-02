module std.xml;

/* R-SLIB-XML-0001: a streaming XML 1.0 reader (pull events over caller fragments), a
   streaming selector over the open-element path and an escaping writer. No DOM, no DTD,
   no external entities: the five predefined entities and character references only, UTF-8
   only. Allocation happens only when a buffer grows. */

enum event_kind {
    none,           // no event is ready
    declaration,    // <?xml ... ?>
    start_element,  // <name attr="value"> or <name/> (the end event follows immediately)
    end_element,    // </name> or the end of <name/>
    text,           // character data with entities decoded
    cdata,          // <![CDATA[ ... ]]>, verbatim
    comment,        // <!-- ... -->
    instruction,    // <?target data?>
};

enum state {
    need_input,     // the fragment was consumed; feed more, or final for the end
    event_ready,    // an event is available through the accessors; feed the rest afterwards
    end,            // the document is complete
};

enum error_code {
    malformed,          // syntax the reader cannot accept at this offset
    unsupported,        // DOCTYPE, a non-predefined entity, or an encoding other than UTF-8
    invalid_utf8,       // a byte sequence that is not UTF-8
    unbalanced,         // an end tag that does not close the open element, or a missing end
    depth_limit,        // more open elements than the reader allows
    attribute_limit,    // more attributes than the reader allows
    duplicate_attribute,
    invalid_selector,   // a selector pattern the compiler cannot accept
    misplaced,          // a writer operation outside the state that permits it
    finished,           // a step after end
    poisoned,           // a step after a failure
};

error error {
    error_code code;
    usize offset;   // byte offset in the whole document (reader), in the pattern (selector), or zero
};

struct progress {
    usize consumed;
    state state;
};

struct options {
    usize max_depth;
    usize max_attributes;
    bool skip_whitespace;   // whitespace-only text between tags is not reported
};

options default_options() {
    return options { .max_depth = 256usize, .max_attributes = 256usize, .skip_whitespace = true };
}

protected const u8 BYTE_LT = 60;
protected const u8 BYTE_GT = 62;
protected const u8 BYTE_SLASH = 47;
protected const u8 BYTE_QUESTION = 63;
protected const u8 BYTE_BANG = 33;
protected const u8 BYTE_MINUS = 45;
protected const u8 BYTE_EQUAL = 61;
protected const u8 BYTE_QUOTE = 34;
protected const u8 BYTE_APOS = 39;
protected const u8 BYTE_AMP = 38;
protected const u8 BYTE_SEMI = 59;
protected const u8 BYTE_HASH = 35;
protected const u8 BYTE_COLON = 58;
protected const u8 BYTE_LBRACKET = 91;
protected const u8 BYTE_RBRACKET = 93;
protected const u8 BYTE_AT = 64;
protected const u8 BYTE_STAR = 42;
protected const usize ENTITY_MAX = 12usize;

/* A prefix or the whole text of one open element, kept while it is open. */
protected struct frame {
    usize name_start;
    usize name_len;
    usize prefix_len;
    usize namespace_count;
};

protected struct namespace_entry {
    usize prefix_start;
    usize prefix_len;
    usize uri_start;
    usize uri_len;
};

protected struct attribute_span {
    usize name_start;
    usize name_len;
    usize value_start;
    usize value_len;
};

protected bool is_space(u8 value) {
    return (value == 32) || (value == 9) || (value == 10) || (value == 13);
}

protected bool is_name_start(u8 value) {
    if ((value >= 97) && (value <= 122)) { return true; }
    if ((value >= 65) && (value <= 90)) { return true; }
    return (value == 95) || (value == BYTE_COLON) || (value >= 128);
}

protected bool is_name_byte(u8 value) {
    if (is_name_start(value) == true) { return true; }
    if ((value >= 48) && (value <= 57)) { return true; }
    return (value == BYTE_MINUS) || (value == 46);
}

protected void push_byte(array<u8>* target, u8 value) throws std.alloc::alloc_error {
    std.array::reserve(target, 1usize);
    bool failed = false;
    try { std.array::push(target, value); }
    catch (std.array::push_error<u8> failure) { failed = true; }
    if (failed == true) { panic("reserved byte insertion failed"); }
}

protected void push_str(array<u8>* target, str value) throws std.alloc::alloc_error {
    const u8[] bytes = value;
    std.array::reserve(target, len(bytes));
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        bool failed = false;
        try { std.array::push(target, bytes[index]); }
        catch (std.array::push_error<u8> failure) { failed = true; }
        if (failed == true) { panic("reserved byte insertion failed"); }
    }
}

protected void truncate(array<u8>* target, usize length) {
    while (len(*target) > length) {
        o<u8> dropped = std.array::pop(target);
        dropped as void;
    }
}

/* Every stored byte range was validated while it was read; a failure here is a defect. */
protected str stored_text(const array<u8>* source, usize start, usize length) {
    if (length == 0usize) { return ""; }
    const u8[] bytes = std.array::as_slice(source);
    const u8[] view = bytes[start..(start + length)];
    try {
        str text = core::validate_utf8(view);
        return text;
    } catch (core::utf8_error failure) {
        panic("stored XML text is not UTF-8");
    }
}

protected bool same_bytes(const u8[] left, const u8[] right) {
    if (len(left) != len(right)) { return false; }
    for (usize index = 0usize; index < len(left); index += 1usize) {
        if (left[index] != right[index]) { return false; }
    }
    return true;
}

/* Appends the UTF-8 form of one scalar; false for a value that is no character of XML 1.0:
   only tab, line feed, carriage return, U+0020..U+D7FF, U+E000..U+FFFD and U+10000..U+10FFFF
   are. */
protected bool push_scalar(array<u8>* target, u32 scalar) throws std.alloc::alloc_error {
    if ((scalar == 0u32) || (scalar > 0x10ffffu32)) { return false; }
    if ((scalar < 0x20u32) && (scalar != 0x9u32) && (scalar != 0xau32) && (scalar != 0xdu32)) {
        return false;
    }
    if ((scalar >= 0xd800u32) && (scalar <= 0xdfffu32)) { return false; }
    if ((scalar == 0xfffeu32) || (scalar == 0xffffu32)) { return false; }
    if (scalar < 0x80u32) {
        push_byte(target, scalar as u8);
        return true;
    }
    if (scalar < 0x800u32) {
        push_byte(target, (0xc0u32 | (scalar >> 6usize)) as u8);
        push_byte(target, (0x80u32 | (scalar & 0x3fu32)) as u8);
        return true;
    }
    if (scalar < 0x10000u32) {
        push_byte(target, (0xe0u32 | (scalar >> 12usize)) as u8);
        push_byte(target, (0x80u32 | ((scalar >> 6usize) & 0x3fu32)) as u8);
        push_byte(target, (0x80u32 | (scalar & 0x3fu32)) as u8);
        return true;
    }
    push_byte(target, (0xf0u32 | (scalar >> 18usize)) as u8);
    push_byte(target, (0x80u32 | ((scalar >> 12usize) & 0x3fu32)) as u8);
    push_byte(target, (0x80u32 | ((scalar >> 6usize) & 0x3fu32)) as u8);
    push_byte(target, (0x80u32 | (scalar & 0x3fu32)) as u8);
    return true;
}

/* Decodes the entity text between & and ; (exclusive); false when it is not accepted. */
protected bool push_entity(array<u8>* target, const u8[] entity) throws std.alloc::alloc_error {
    usize length = len(entity);
    if (length == 0usize) { return false; }
    u8[3] amp = { 97, 109, 112 };
    u8[2] lt = { 108, 116 };
    u8[2] gt = { 103, 116 };
    u8[4] quot = { 113, 117, 111, 116 };
    u8[4] apos = { 97, 112, 111, 115 };
    const u8[] amp_view = &amp;
    const u8[] lt_view = &lt;
    const u8[] gt_view = &gt;
    const u8[] quot_view = &quot;
    const u8[] apos_view = &apos;
    if (same_bytes(entity, amp_view) == true) { push_byte(target, BYTE_AMP); return true; }
    if (same_bytes(entity, lt_view) == true) { push_byte(target, BYTE_LT); return true; }
    if (same_bytes(entity, gt_view) == true) { push_byte(target, BYTE_GT); return true; }
    if (same_bytes(entity, quot_view) == true) { push_byte(target, BYTE_QUOTE); return true; }
    if (same_bytes(entity, apos_view) == true) { push_byte(target, BYTE_APOS); return true; }
    if ((entity[0] != BYTE_HASH) || (length < 2usize)) { return false; }
    u32 radix = 10u32;
    usize index = 1usize;
    if ((entity[1] == 120) || (entity[1] == 88)) {
        radix = 16u32;
        index = 2usize;
    }
    if (index >= length) { return false; }
    u32 scalar = 0u32;
    while (index < length) {
        u8 digit = entity[index];
        u32 value = 0u32;
        if ((digit >= 48) && (digit <= 57)) { value = (digit - 48) as u32; }
        else { if ((radix == 16u32) && (digit >= 97) && (digit <= 102)) { value = (digit - 87) as u32; }
        else { if ((radix == 16u32) && (digit >= 65) && (digit <= 70)) { value = (digit - 55) as u32; }
        else { return false; } } }
        if (scalar > 0x10ffffu32) { return false; }
        scalar = scalar * radix + value;
        index += 1usize;
    }
    bool pushed = push_scalar(target, scalar);
    return pushed;
}

/* R-SLIB-XML-0003: the reader. Stages: 0 content, 1 after '<', 2 start-tag name,
   3 between attributes, 4 attribute name, 5 expecting '=', 6 expecting a quote,
   7 attribute value, 8 after '/', 9 end-tag name, 10 before the end-tag '>',
   11 instruction target, 12 instruction data, 13 after '<!', 14 comment, 15 CDATA opening,
   16 CDATA, 17 entity, 18 DOCTYPE (rejected). */
struct reader {
    protected array<u8> stack_bytes;
    protected array<frame> frames;
    protected array<namespace_entry> namespaces;
    protected array<u8> event_bytes;
    protected array<attribute_span> attributes;
    protected options settings;
    protected u8 stage;
    protected u8 return_stage;
    protected event_kind current;
    protected usize total_in;
    protected usize name_start;
    protected usize name_len;
    protected usize prefix_len;
    protected usize text_start;
    protected usize text_len;
    protected bool name_in_stack;
    protected bool text_has_content;
    protected u8[16] entity;
    protected usize entity_len;
    protected u8 quote;
    protected usize attribute_name_start;
    protected usize attribute_name_len;
    protected usize attribute_value_start;
    protected bool pending_end;
    protected bool root_seen;
    protected bool root_closed;
    protected bool poisoned;
    protected bool ended;
    protected u8 utf8_remaining;
    protected usize matched;
    protected usize tag_start;
    /* The `]` bytes that the character data ends with, for the `]]>` it may not hold. */
    protected usize brackets;
};

reader reader::create(options settings) throws std.alloc::alloc_error {
    array<u8> stack_bytes = std.array::create::<u8>();
    array<frame> frames = std.array::create::<frame>();
    array<namespace_entry> namespaces = std.array::create::<namespace_entry>();
    array<u8> event_bytes = std.array::create::<u8>();
    array<attribute_span> attributes = std.array::create::<attribute_span>();
    reader result = reader {
        .stack_bytes = move stack_bytes,
        .frames = move frames,
        .namespaces = move namespaces,
        .event_bytes = move event_bytes,
        .attributes = move attributes,
        .settings = settings,
        .current = event_kind::none,
    };
    result.reset();
    return move result;
}

/* Returns to the start of a new document, keeping every allocation. */
void reader::reset(reader* this) {
    std.array::clear(&this->stack_bytes);
    std.array::clear(&this->frames);
    std.array::clear(&this->namespaces);
    std.array::clear(&this->event_bytes);
    std.array::clear(&this->attributes);
    this->stage = 0;
    this->return_stage = 0;
    this->current = event_kind::none;
    this->total_in = 0usize;
    this->name_start = 0usize;
    this->name_len = 0usize;
    this->prefix_len = 0usize;
    this->text_start = 0usize;
    this->text_len = 0usize;
    this->name_in_stack = false;
    this->text_has_content = false;
    this->entity_len = 0usize;
    this->quote = 0;
    this->attribute_name_start = 0usize;
    this->attribute_name_len = 0usize;
    this->attribute_value_start = 0usize;
    this->pending_end = false;
    this->root_seen = false;
    this->root_closed = false;
    this->poisoned = false;
    this->ended = false;
    this->utf8_remaining = 0;
    this->matched = 0usize;
    this->tag_start = 0usize;
    this->brackets = 0usize;
}

usize reader::depth(const reader* this) { return len(this->frames); }
usize reader::offset(const reader* this) { return this->total_in; }
event_kind reader::kind(const reader* this) { return this->current; }

protected frame reader::top(const reader* this) {
    const frame[] frames = std.array::as_slice(&this->frames);
    return frames[len(frames) - 1usize];
}

/* Validates one byte of UTF-8 as it is stored; false on an invalid sequence. */
protected bool reader::accept_utf8(reader* this, u8 value) {
    if (this->utf8_remaining != 0) {
        if ((value & 0xc0) != 0x80) { return false; }
        this->utf8_remaining -= 1;
        return true;
    }
    if (value < 0x80) { return true; }
    if ((value >= 0xc2) && (value <= 0xdf)) { this->utf8_remaining = 1; return true; }
    if ((value >= 0xe0) && (value <= 0xef)) { this->utf8_remaining = 2; return true; }
    if ((value >= 0xf0) && (value <= 0xf4)) { this->utf8_remaining = 3; return true; }
    return false;
}

protected void reader::begin_event(reader* this) {
    std.array::clear(&this->event_bytes);
    std.array::clear(&this->attributes);
    this->current = event_kind::none;
    this->text_start = 0usize;
    this->text_len = 0usize;
    this->text_has_content = false;
    this->name_in_stack = false;
    this->name_start = 0usize;
    this->name_len = 0usize;
    this->prefix_len = 0usize;
}

/* Records the namespace declarations of the just-read start tag on the stack. */
protected void reader::declare_namespaces(reader* this) throws std.alloc::alloc_error {
    usize count = len(this->attributes);
    usize declared = 0usize;
    for (usize index = 0usize; index < count; index += 1usize) {
        const attribute_span[] spans = std.array::as_slice(&this->attributes);
        attribute_span span = spans[index];
        const u8[] event = std.array::as_slice(&this->event_bytes);
        const u8[] name = event[span.name_start..(span.name_start + span.name_len)];
        bool is_default = (span.name_len == 5usize) && (name[0] == 120) && (name[1] == 109) &&
                          (name[2] == 108) && (name[3] == 110) && (name[4] == 115);
        bool is_prefixed = (span.name_len > 6usize) && (name[0] == 120) && (name[1] == 109) &&
                           (name[2] == 108) && (name[3] == 110) && (name[4] == 115) && (name[5] == BYTE_COLON);
        if ((is_default == false) && (is_prefixed == false)) {
            declared = declared;
        } else {
            usize prefix_start = len(this->stack_bytes);
            usize prefix_len = (is_prefixed == true) ? span.name_len - 6usize : 0usize;
            for (usize at = 0usize; at < prefix_len; at += 1usize) {
                const u8[] bytes = std.array::as_slice(&this->event_bytes);
                u8 byte = bytes[span.name_start + 6usize + at];
                push_byte(&this->stack_bytes, byte);
            }
            usize uri_start = len(this->stack_bytes);
            for (usize at = 0usize; at < span.value_len; at += 1usize) {
                const u8[] bytes = std.array::as_slice(&this->event_bytes);
                u8 byte = bytes[span.value_start + at];
                push_byte(&this->stack_bytes, byte);
            }
            namespace_entry entry = namespace_entry {
                .prefix_start = prefix_start, .prefix_len = prefix_len,
                .uri_start = uri_start, .uri_len = span.value_len,
            };
            bool failed = false;
            std.array::reserve(&this->namespaces, 1usize);
            try { std.array::push(&this->namespaces, entry); }
            catch (std.array::push_error<namespace_entry> failure) { failed = true; }
            if (failed == true) { panic("reserved namespace insertion failed"); }
            declared += 1usize;
        }
    }
    usize depth = len(this->frames);
    frame[] frames = std.array::as_slice_mut(&this->frames);
    frames[depth - 1usize].namespace_count = declared;
}

protected void reader::pop_frame(reader* this) {
    o<frame> popped = std.array::pop(&this->frames);
    switch (popped) {
    case variant o::some(top):
        usize namespaces = top->namespace_count;
        for (usize index = 0usize; index < namespaces; index += 1usize) {
            o<namespace_entry> entry = std.array::pop(&this->namespaces);
            entry as void;
        }
        truncate(&this->stack_bytes, top->name_start);
        break;
    case variant o::none:
        break;
    }
}

protected bool reader::finish_entity(reader* this, bool into_stack) throws std.alloc::alloc_error {
    u8[16] entity = this->entity;
    const u8[] entity_view = entity[0usize..this->entity_len];
    bool pushed = false;
    if (into_stack == true) {
        pushed = push_entity(&this->stack_bytes, entity_view);
    } else {
        pushed = push_entity(&this->event_bytes, entity_view);
    }
    this->entity_len = 0usize;
    return pushed;
}

/* Outcomes of one byte in one stage. */
protected const u8 STEP_CONTINUE = 0;
protected const u8 STEP_READY = 1;
protected const u8 STEP_READY_KEEP = 2;   // an event is ready and the byte was not consumed
protected const u8 STEP_MALFORMED = 3;
protected const u8 STEP_UNSUPPORTED = 4;
protected const u8 STEP_INVALID_UTF8 = 5;
protected const u8 STEP_UNBALANCED = 6;
protected const u8 STEP_DEPTH = 7;
protected const u8 STEP_ATTRIBUTES = 8;
protected const u8 STEP_DUPLICATE = 9;

protected u8 reader::store_event_byte(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (this->accept_utf8(byte) == false) { return STEP_INVALID_UTF8; }
    push_byte(&this->event_bytes, byte);
    return STEP_CONTINUE;
}

protected void reader::begin_entity(reader* this, u8 return_stage) {
    this->return_stage = return_stage;
    this->stage = 17;
    this->entity_len = 0usize;
}

/* Stage 0: character data between tags; XML 1.0 excludes `]]>` from it. */
protected u8 reader::stage_content(reader* this, u8 byte, usize offset) throws std.alloc::alloc_error {
    if ((byte == BYTE_GT) && (this->brackets >= 2usize)) { return STEP_MALFORMED; }
    if (byte == BYTE_RBRACKET) { this->brackets += 1usize; } else { this->brackets = 0usize; }
    if (byte == BYTE_LT) {
        if (len(this->event_bytes) != 0usize) {
            if ((this->settings.skip_whitespace == false) || (this->text_has_content == true)) {
                this->text_len = len(this->event_bytes);
                this->current = event_kind::text;
                return STEP_READY_KEEP;
            }
            std.array::clear(&this->event_bytes);
        }
        this->tag_start = offset;
        this->stage = 1;
        return STEP_CONTINUE;
    }
    if (byte == BYTE_AMP) {
        if (len(this->frames) == 0usize) { return STEP_MALFORMED; }
        this->begin_entity(0);
        return STEP_CONTINUE;
    }
    if (len(this->frames) == 0usize) {
        if (is_space(byte) == true) { return STEP_CONTINUE; }
        return STEP_MALFORMED;
    }
    if (is_space(byte) == false) { this->text_has_content = true; }
    return this->store_event_byte(byte);
}

/* Stage 1: the byte after '<'. */
protected u8 reader::stage_tag_open(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (byte == BYTE_SLASH) {
        this->stage = 9;
        this->name_start = len(this->event_bytes);
        return STEP_CONTINUE;
    }
    if (byte == BYTE_QUESTION) {
        this->stage = 11;
        this->name_start = len(this->event_bytes);
        return STEP_CONTINUE;
    }
    if (byte == BYTE_BANG) {
        this->stage = 13;
        this->matched = 0usize;
        return STEP_CONTINUE;
    }
    if (is_name_start(byte) == false) { return STEP_MALFORMED; }
    if (this->root_closed == true) { return STEP_MALFORMED; }
    if (len(this->frames) >= this->settings.max_depth) { return STEP_DEPTH; }
    usize start = len(this->stack_bytes);
    frame opened = frame { .name_start = start, .name_len = 0usize, .prefix_len = 0usize, .namespace_count = 0usize };
    std.array::reserve(&this->frames, 1usize);
    bool pushed = true;
    try { std.array::push(&this->frames, opened); }
    catch (std.array::push_error<frame> rejected) { pushed = false; }
    if (pushed == false) { panic("reserved frame insertion failed"); }
    if (this->accept_utf8(byte) == false) { return STEP_INVALID_UTF8; }
    push_byte(&this->stack_bytes, byte);
    this->name_start = start;
    this->name_len = 1usize;
    this->prefix_len = 0usize;
    this->name_in_stack = true;
    this->root_seen = true;
    this->stage = 2;
    return STEP_CONTINUE;
}

/* A start tag is complete: the frame learns its name and namespace declarations. */
protected u8 reader::complete_start_tag(reader* this, bool empty) throws std.alloc::alloc_error {
    usize depth = len(this->frames);
    frame[] frames = std.array::as_slice_mut(&this->frames);
    frames[depth - 1usize].name_len = this->name_len;
    frames[depth - 1usize].prefix_len = this->prefix_len;
    this->pending_end = empty;
    this->current = event_kind::start_element;
    this->stage = 0;
    this->declare_namespaces();
    return STEP_READY;
}

/* Stage 2: the start-tag name. */
protected u8 reader::stage_start_name(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (is_name_byte(byte) == true) {
        if (this->accept_utf8(byte) == false) { return STEP_INVALID_UTF8; }
        if (byte == BYTE_COLON) { this->prefix_len = this->name_len; }
        push_byte(&this->stack_bytes, byte);
        this->name_len += 1usize;
        return STEP_CONTINUE;
    }
    if (is_space(byte) == true) { this->stage = 3; return STEP_CONTINUE; }
    if (byte == BYTE_SLASH) { this->stage = 8; return STEP_CONTINUE; }
    if (byte == BYTE_GT) { return this->complete_start_tag(false); }
    return STEP_MALFORMED;
}

/* Stage 3: between attributes. */
protected u8 reader::stage_attributes(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (is_space(byte) == true) { return STEP_CONTINUE; }
    if (byte == BYTE_SLASH) { this->stage = 8; return STEP_CONTINUE; }
    if (byte == BYTE_GT) { return this->complete_start_tag(false); }
    if (is_name_start(byte) == false) { return STEP_MALFORMED; }
    if (len(this->attributes) >= this->settings.max_attributes) { return STEP_ATTRIBUTES; }
    this->attribute_name_start = len(this->event_bytes);
    this->attribute_name_len = 1usize;
    this->stage = 4;
    return this->store_event_byte(byte);
}

/* Stage 4: an attribute name. */
protected u8 reader::stage_attribute_name(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (is_name_byte(byte) == true) {
        this->attribute_name_len += 1usize;
        return this->store_event_byte(byte);
    }
    if (byte == BYTE_EQUAL) { this->stage = 6; return STEP_CONTINUE; }
    if (is_space(byte) == true) { this->stage = 5; return STEP_CONTINUE; }
    return STEP_MALFORMED;
}

/* Stage 5: expecting '='; stage 6: expecting the opening quote. */
protected u8 reader::stage_attribute_separator(reader* this, u8 byte) {
    if (this->stage == 5) {
        if (byte == BYTE_EQUAL) { this->stage = 6; return STEP_CONTINUE; }
        if (is_space(byte) == true) { return STEP_CONTINUE; }
        return STEP_MALFORMED;
    }
    if ((byte == BYTE_QUOTE) || (byte == BYTE_APOS)) {
        this->quote = byte;
        this->attribute_value_start = len(this->event_bytes);
        this->stage = 7;
        return STEP_CONTINUE;
    }
    if (is_space(byte) == true) { return STEP_CONTINUE; }
    return STEP_MALFORMED;
}

/* Stage 7: an attribute value up to its closing quote. */
protected u8 reader::stage_attribute_value(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (byte == this->quote) {
        usize value_len = len(this->event_bytes) - this->attribute_value_start;
        attribute_span span = attribute_span {
            .name_start = this->attribute_name_start, .name_len = this->attribute_name_len,
            .value_start = this->attribute_value_start, .value_len = value_len,
        };
        bool duplicate = false;
        {
            const attribute_span[] existing = std.array::as_slice(&this->attributes);
            const u8[] event = std.array::as_slice(&this->event_bytes);
            const u8[] name = event[span.name_start..(span.name_start + span.name_len)];
            for (usize index = 0usize; index < len(existing); index += 1usize) {
                attribute_span other_span = existing[index];
                const u8[] other = event[other_span.name_start..(other_span.name_start + other_span.name_len)];
                if (same_bytes(name, other) == true) { duplicate = true; }
            }
        }
        if (duplicate == true) { return STEP_DUPLICATE; }
        std.array::reserve(&this->attributes, 1usize);
        bool pushed = true;
        try { std.array::push(&this->attributes, span); }
        catch (std.array::push_error<attribute_span> rejected) { pushed = false; }
        if (pushed == false) { panic("reserved attribute insertion failed"); }
        this->stage = 3;
        return STEP_CONTINUE;
    }
    if (byte == BYTE_AMP) {
        this->begin_entity(7);
        return STEP_CONTINUE;
    }
    if (byte == BYTE_LT) { return STEP_MALFORMED; }
    u8 stored = ((byte == 9) || (byte == 10) || (byte == 13)) ? 32 : byte;
    return this->store_event_byte(stored);
}

/* Stage 8: after '/' in a start tag; stage 10: whitespace before an end tag's '>'. */
protected u8 reader::stage_tag_close(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (this->stage == 8) {
        if (byte != BYTE_GT) { return STEP_MALFORMED; }
        return this->complete_start_tag(true);
    }
    if (byte == BYTE_GT) { this->stage = 0; return STEP_READY; }
    if (is_space(byte) == true) { return STEP_CONTINUE; }
    return STEP_MALFORMED;
}

/* Stage 9: an end-tag name, matched against the open element. */
protected u8 reader::stage_end_name(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (is_name_byte(byte) == true) {
        this->name_len += 1usize;
        return this->store_event_byte(byte);
    }
    if ((byte != BYTE_GT) && (is_space(byte) == false)) { return STEP_MALFORMED; }
    if (len(this->frames) == 0usize) { return STEP_UNBALANCED; }
    frame top = this->top();
    {
        const u8[] stack = std.array::as_slice(&this->stack_bytes);
        const u8[] event = std.array::as_slice(&this->event_bytes);
        const u8[] open_name = stack[top.name_start..(top.name_start + top.name_len)];
        const u8[] close_name = event[this->name_start..(this->name_start + this->name_len)];
        bool matches = same_bytes(open_name, close_name);
        if (matches == false) { return STEP_UNBALANCED; }
    }
    this->prefix_len = top.prefix_len;
    this->pop_frame();
    this->current = event_kind::end_element;
    if (len(this->frames) == 0usize) { this->root_closed = true; }
    if (byte == BYTE_GT) { this->stage = 0; return STEP_READY; }
    this->stage = 10;
    return STEP_CONTINUE;
}

/* Stage 11: a processing-instruction target. */
protected u8 reader::stage_instruction_target(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (is_name_byte(byte) == true) {
        this->name_len += 1usize;
        return this->store_event_byte(byte);
    }
    if ((is_space(byte) == false) && (byte != BYTE_QUESTION)) { return STEP_MALFORMED; }
    if (this->name_len == 0usize) { return STEP_MALFORMED; }
    this->text_start = len(this->event_bytes);
    this->stage = 12;
    this->matched = (byte == BYTE_QUESTION) ? 1usize : 0usize;
    return STEP_CONTINUE;
}

protected bool reader::is_xml_target(const reader* this) {
    const u8[] event = std.array::as_slice(&this->event_bytes);
    const u8[] target = event[this->name_start..(this->name_start + this->name_len)];
    return (this->name_len == 3usize) && ((target[0] | 32) == 120) &&
           ((target[1] | 32) == 109) && ((target[2] | 32) == 108);
}

/* Stage 12: processing-instruction data up to "?>"; the xml target is the declaration. */
protected u8 reader::stage_instruction_data(reader* this, u8 byte) throws std.alloc::alloc_error {
    if ((this->matched == 1usize) && (byte == BYTE_GT)) {
        usize text_len = len(this->event_bytes) - this->text_start;
        bool is_xml = this->is_xml_target();
        usize lead = 0usize;
        {
            const u8[] event = std.array::as_slice(&this->event_bytes);
            const u8[] data = event[this->text_start..(this->text_start + text_len)];
            while ((lead < text_len) && (is_space(data[lead]) == true)) { lead += 1usize; }
        }
        this->text_len = text_len;
        this->current = (is_xml == true) ? event_kind::declaration : event_kind::instruction;
        if (is_xml == true) {
            if ((this->root_seen == true) || (this->tag_start != 0usize)) { return STEP_MALFORMED; }
            if (this->has_unsupported_encoding() == true) { return STEP_UNSUPPORTED; }
        }
        this->text_start += lead;
        this->text_len -= lead;
        this->stage = 0;
        return STEP_READY;
    }
    if (this->matched == 1usize) { push_byte(&this->event_bytes, BYTE_QUESTION); }
    this->matched = 0usize;
    if (byte == BYTE_QUESTION) {
        this->matched = 1usize;
        return STEP_CONTINUE;
    }
    return this->store_event_byte(byte);
}

/* Stage 13: after "<!": a comment, a CDATA section or a rejected DOCTYPE. */
protected u8 reader::stage_bang(reader* this, u8 byte) {
    if ((this->matched == 0usize) && (byte == BYTE_MINUS)) { this->matched = 1usize; return STEP_CONTINUE; }
    if ((this->matched == 1usize) && (byte == BYTE_MINUS)) {
        this->stage = 14;
        this->matched = 0usize;
        this->text_start = len(this->event_bytes);
        return STEP_CONTINUE;
    }
    if ((this->matched == 0usize) && (byte == BYTE_LBRACKET)) {
        this->stage = 15;
        this->matched = 0usize;
        return STEP_CONTINUE;
    }
    if ((this->matched == 0usize) && (byte == 68)) { return STEP_UNSUPPORTED; }
    return STEP_MALFORMED;
}

/* Stage 14: comment text up to "-->"; "--" inside a comment is malformed. */
protected u8 reader::stage_comment(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (byte == BYTE_MINUS) {
        if (this->matched >= 2usize) { return STEP_MALFORMED; }
        this->matched += 1usize;
        return STEP_CONTINUE;
    }
    if ((byte == BYTE_GT) && (this->matched == 2usize)) {
        this->text_len = len(this->event_bytes) - this->text_start;
        this->current = event_kind::comment;
        this->stage = 0;
        return STEP_READY;
    }
    if (this->matched == 2usize) { return STEP_MALFORMED; }
    while (this->matched > 0usize) {
        push_byte(&this->event_bytes, BYTE_MINUS);
        this->matched -= 1usize;
    }
    return this->store_event_byte(byte);
}

/* Stage 15: the "[CDATA[" opening; stage 16: the section up to "]]>". */
protected u8 reader::stage_cdata(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (this->stage == 15) {
        u8[6] opening = { 67, 68, 65, 84, 65, BYTE_LBRACKET };
        if (byte != opening[this->matched]) { return STEP_MALFORMED; }
        this->matched += 1usize;
        if (this->matched == 6usize) {
            if (len(this->frames) == 0usize) { return STEP_MALFORMED; }
            this->stage = 16;
            this->matched = 0usize;
            this->text_start = len(this->event_bytes);
        }
        return STEP_CONTINUE;
    }
    if (byte == BYTE_RBRACKET) {
        if (this->matched < 2usize) {
            this->matched += 1usize;
            return STEP_CONTINUE;
        }
        push_byte(&this->event_bytes, BYTE_RBRACKET);
        return STEP_CONTINUE;
    }
    if ((byte == BYTE_GT) && (this->matched == 2usize)) {
        this->text_len = len(this->event_bytes) - this->text_start;
        this->current = event_kind::cdata;
        this->stage = 0;
        return STEP_READY;
    }
    while (this->matched > 0usize) {
        push_byte(&this->event_bytes, BYTE_RBRACKET);
        this->matched -= 1usize;
    }
    return this->store_event_byte(byte);
}

/* Stage 17: an entity between '&' and ';'. */
protected u8 reader::stage_entity(reader* this, u8 byte) throws std.alloc::alloc_error {
    if (byte == BYTE_SEMI) {
        bool decoded = this->finish_entity(false);
        if (decoded == false) { return STEP_UNSUPPORTED; }
        if (this->return_stage == 0) { this->text_has_content = true; }
        this->stage = this->return_stage;
        return STEP_CONTINUE;
    }
    bool acceptable = (byte == BYTE_HASH) || ((is_name_byte(byte) == true) && (byte < 128));
    if ((this->entity_len >= ENTITY_MAX) || (acceptable == false)) { return STEP_MALFORMED; }
    this->entity[this->entity_len] = byte;
    this->entity_len += 1usize;
    return STEP_CONTINUE;
}

protected u8 reader::step(reader* this, u8 byte, usize offset) throws std.alloc::alloc_error {
    switch (this->stage) {
    case 0: return this->stage_content(byte, offset);
    case 1: return this->stage_tag_open(byte);
    case 2: return this->stage_start_name(byte);
    case 3: return this->stage_attributes(byte);
    case 4: return this->stage_attribute_name(byte);
    case 5: fallthrough;
    case 6: return this->stage_attribute_separator(byte);
    case 7: return this->stage_attribute_value(byte);
    case 8: fallthrough;
    case 10: return this->stage_tag_close(byte);
    case 9: return this->stage_end_name(byte);
    case 11: return this->stage_instruction_target(byte);
    case 12: return this->stage_instruction_data(byte);
    case 13: return this->stage_bang(byte);
    case 14: return this->stage_comment(byte);
    case 15: fallthrough;
    case 16: return this->stage_cdata(byte);
    case 17: return this->stage_entity(byte);
    default: return STEP_MALFORMED;
    }
}

protected error_code failure_of(u8 outcome) {
    if (outcome == STEP_UNSUPPORTED) { return error_code::unsupported; }
    if (outcome == STEP_INVALID_UTF8) { return error_code::invalid_utf8; }
    if (outcome == STEP_UNBALANCED) { return error_code::unbalanced; }
    if (outcome == STEP_DEPTH) { return error_code::depth_limit; }
    if (outcome == STEP_ATTRIBUTES) { return error_code::attribute_limit; }
    if (outcome == STEP_DUPLICATE) { return error_code::duplicate_attribute; }
    return error_code::malformed;
}

/* The end of an empty element follows its start without consuming input. */
protected void reader::emit_pending_end(reader* this) throws std.alloc::alloc_error {
    this->pending_end = false;
    this->begin_event();
    frame top = this->top();
    usize start = len(this->event_bytes);
    for (usize index = 0usize; index < top.name_len; index += 1usize) {
        const u8[] stack = std.array::as_slice(&this->stack_bytes);
        u8 byte = stack[top.name_start + index];
        push_byte(&this->event_bytes, byte);
    }
    this->name_start = start;
    this->name_len = top.name_len;
    this->prefix_len = top.prefix_len;
    this->pop_frame();
    this->current = event_kind::end_element;
    if (len(this->frames) == 0usize) { this->root_closed = true; }
}

/* R-SLIB-XML-0003: feeds one fragment. */
progress reader::feed(reader* this, const u8[] fragment, bool final) throws error, std.alloc::alloc_error {
    throw (this->poisoned == true) error { .code = error_code::poisoned, .offset = this->total_in };
    throw (this->ended == true) error { .code = error_code::finished, .offset = this->total_in };
    if (this->pending_end == true) {
        this->emit_pending_end();
        return progress { .consumed = 0usize, .state = state::event_ready };
    }
    /* Only a delivered event is discarded; a token split across fragments stays. */
    if (this->current != event_kind::none) { this->begin_event(); }
    usize pos = 0usize;
    usize size = len(fragment);
    u8 outcome = STEP_CONTINUE;
    while ((pos < size) && (outcome == STEP_CONTINUE)) {
        u8 byte = fragment[pos];
        usize offset = this->total_in + pos;
        outcome = this->step(byte, offset);
        if (outcome != STEP_READY_KEEP) { pos += 1usize; }
    }
    this->total_in += pos;
    if ((outcome != STEP_CONTINUE) && (outcome != STEP_READY) && (outcome != STEP_READY_KEEP)) {
        this->poisoned = true;
        throw error { .code = failure_of(outcome), .offset = this->total_in };
    }
    if (outcome != STEP_CONTINUE) { return progress { .consumed = pos, .state = state::event_ready }; }
    if (final == false) { return progress { .consumed = pos, .state = state::need_input }; }
    if ((this->stage == 0) && (len(this->event_bytes) != 0usize)) {
        if ((this->settings.skip_whitespace == false) || (this->text_has_content == true)) {
            this->text_len = len(this->event_bytes);
            this->current = event_kind::text;
            return progress { .consumed = pos, .state = state::event_ready };
        }
        std.array::clear(&this->event_bytes);
    }
    if ((this->stage != 0) || (this->root_seen == false) || (len(this->frames) != 0usize)) {
        this->poisoned = true;
        error_code code = (len(this->frames) != 0usize) ? error_code::unbalanced : error_code::malformed;
        throw error { .code = code, .offset = this->total_in };
    }
    this->ended = true;
    return progress { .consumed = pos, .state = state::end };
}

/* The quoted value that follows "encoding" in the declaration, as a byte range of the
   event buffer; none when the declaration names no encoding. */
protected o<attribute_span> reader::encoding_span(const reader* this) {
    const u8[] event = std.array::as_slice(&this->event_bytes);
    const u8[] data = event[this->text_start..(this->text_start + this->text_len)];
    u8[8] key = { 101, 110, 99, 111, 100, 105, 110, 103 };
    usize length = len(data);
    for (usize index = 0usize; index + 8usize <= length; index += 1usize) {
        bool found = true;
        for (usize at = 0usize; at < 8usize; at += 1usize) {
            if (data[index + at] != key[at]) { found = false; }
        }
        if (found == true) {
            usize cursor = index + 8usize;
            while ((cursor < length) && ((is_space(data[cursor]) == true) || (data[cursor] == BYTE_EQUAL))) { cursor += 1usize; }
            if (cursor >= length) { return o::some(attribute_span { .name_start = 0usize, .name_len = 0usize, .value_start = 0usize, .value_len = 0usize }); }
            u8 quote = data[cursor];
            cursor += 1usize;
            usize value_start = cursor;
            while ((cursor < length) && (data[cursor] != quote)) { cursor += 1usize; }
            attribute_span span = attribute_span {
                .name_start = 0usize, .name_len = 0usize,
                .value_start = this->text_start + value_start, .value_len = cursor - value_start,
            };
            return o::some(span);
        }
    }
    return o::none;
}

/* Any declared encoding other than UTF-8, in any letter case, is unsupported. */
protected bool reader::has_unsupported_encoding(const reader* this) {
    o<attribute_span> found = this->encoding_span();
    switch (found) {
    case variant o::some(span):
        if (span->value_len != 5usize) { return true; }
        const u8[] event = std.array::as_slice(&this->event_bytes);
        const u8[] value = event[span->value_start..(span->value_start + span->value_len)];
        u8[5] utf8 = { 117, 116, 102, 45, 56 };
        for (usize index = 0usize; index < 5usize; index += 1usize) {
            u8 lower = value[index];
            if ((lower >= 65) && (lower <= 90)) { lower += 32; }
            if (lower != utf8[index]) { return true; }
        }
        return false;
    case variant o::none:
        return false;
    }
}

/* R-SLIB-XML-0004: accessors of the current event; strings borrow from the reader. */
str reader::name(const reader* this) {
    if (this->name_in_stack == true) {
        return stored_text(&this->stack_bytes, this->name_start, this->name_len);
    }
    return stored_text(&this->event_bytes, this->name_start, this->name_len);
}

str reader::prefix(const reader* this) {
    if (this->name_in_stack == true) {
        return stored_text(&this->stack_bytes, this->name_start, this->prefix_len);
    }
    return stored_text(&this->event_bytes, this->name_start, this->prefix_len);
}

str reader::local_name(const reader* this) {
    usize skip = (this->prefix_len == 0usize) ? 0usize : this->prefix_len + 1usize;
    if (this->name_in_stack == true) {
        return stored_text(&this->stack_bytes, this->name_start + skip, this->name_len - skip);
    }
    return stored_text(&this->event_bytes, this->name_start + skip, this->name_len - skip);
}

/* The namespace URI of the current element, from the innermost declaration of its prefix
   (the default namespace for an unprefixed name); empty when undeclared. */
str reader::namespace(const reader* this) {
    str prefix = this->prefix();
    const u8[] prefix_bytes = prefix;
    const namespace_entry[] entries = std.array::as_slice(&this->namespaces);
    usize index = len(entries);
    while (index > 0usize) {
        index -= 1usize;
        namespace_entry entry = entries[index];
        const u8[] stack = std.array::as_slice(&this->stack_bytes);
        const u8[] declared = stack[entry.prefix_start..(entry.prefix_start + entry.prefix_len)];
        if (same_bytes(declared, prefix_bytes) == true) {
            return stored_text(&this->stack_bytes, entry.uri_start, entry.uri_len);
        }
    }
    return "";
}

str reader::text(const reader* this) {
    return stored_text(&this->event_bytes, this->text_start, this->text_len);
}

usize reader::attribute_count(const reader* this) { return len(this->attributes); }

str reader::attribute_name(const reader* this, usize index) {
    const attribute_span[] spans = std.array::as_slice(&this->attributes);
    attribute_span span = spans[index];
    return stored_text(&this->event_bytes, span.name_start, span.name_len);
}

str reader::attribute_value(const reader* this, usize index) {
    const attribute_span[] spans = std.array::as_slice(&this->attributes);
    attribute_span span = spans[index];
    return stored_text(&this->event_bytes, span.value_start, span.value_len);
}

o<str> reader::attribute(const reader* this, str name) {
    const u8[] wanted = name;
    const attribute_span[] spans = std.array::as_slice(&this->attributes);
    for (usize index = 0usize; index < len(spans); index += 1usize) {
        attribute_span span = spans[index];
        const u8[] event = std.array::as_slice(&this->event_bytes);
        const u8[] candidate = event[span.name_start..(span.name_start + span.name_len)];
        if (same_bytes(candidate, wanted) == true) {
            str value = stored_text(&this->event_bytes, span.value_start, span.value_len);
            return o::some(value);
        }
    }
    return o::none;
}

/* The qualified name of the open element at the given depth (zero is the root). */
str reader::path_name(const reader* this, usize depth) {
    const frame[] frames = std.array::as_slice(&this->frames);
    frame entry = frames[depth];
    return stored_text(&this->stack_bytes, entry.name_start, entry.name_len);
}

/* R-SLIB-XML-0005: a streaming selector over the path of open elements. Pattern grammar:
   ["/" | "//"] step { ("/" | "//") step }; step = ("*" | name) { "[" "@" name ["=" quoted] "]" }.
   A leading "/" anchors the first step at the root; otherwise it matches at any depth.
   "/" between steps selects a child, "//" a descendant. Predicates are permitted on the
   last step only, where the current element's attributes are known. */
protected struct step {
    usize name_start;
    usize name_len;
    bool any_name;
    bool descendant;
    usize predicate_start;
    usize predicate_count;
};

protected struct predicate {
    usize name_start;
    usize name_len;
    bool has_value;
    usize value_start;
    usize value_len;
};

protected const usize SELECTOR_MAX_DEPTH = 512usize;

struct selector {
    protected array<u8> bytes;
    protected array<step> steps;
    protected array<predicate> predicates;
    protected bool absolute;
};

protected void push_step(array<step>* target, step value) throws std.alloc::alloc_error {
    std.array::reserve(target, 1usize);
    bool failed = false;
    try { std.array::push(target, value); }
    catch (std.array::push_error<step> rejected) { failed = true; }
    if (failed == true) { panic("reserved step insertion failed"); }
}

protected void push_predicate(array<predicate>* target, predicate value) throws std.alloc::alloc_error {
    std.array::reserve(target, 1usize);
    bool failed = false;
    try { std.array::push(target, value); }
    catch (std.array::push_error<predicate> rejected) { failed = true; }
    if (failed == true) { panic("reserved predicate insertion failed"); }
}

selector selector::compile(str pattern) throws error, std.alloc::alloc_error {
    const u8[] source = pattern;
    usize length = len(source);
    array<u8> bytes = std.array::create::<u8>();
    array<step> steps = std.array::create::<step>();
    array<predicate> predicates = std.array::create::<predicate>();
    bool absolute = false;
    usize at = 0usize;
    bool descendant = true;
    if ((length > 0usize) && (source[0] == BYTE_SLASH)) {
        at = 1usize;
        absolute = true;
        descendant = false;
        if ((length > 1usize) && (source[1] == BYTE_SLASH)) {
            at = 2usize;
            absolute = false;
            descendant = true;
        }
    }
    throw (at >= length) error { .code = error_code::invalid_selector, .offset = at };
    while (at < length) {
        step current = step {
            .name_start = len(bytes), .name_len = 0usize, .any_name = false, .descendant = descendant,
            .predicate_start = len(predicates), .predicate_count = 0usize,
        };
        if (source[at] == BYTE_STAR) {
            current.any_name = true;
            at += 1usize;
        } else {
            while ((at < length) && (is_name_byte(source[at]) == true)) {
                push_byte(&bytes, source[at]);
                current.name_len += 1usize;
                at += 1usize;
            }
            throw (current.name_len == 0usize) error { .code = error_code::invalid_selector, .offset = at };
        }
        while ((at < length) && (source[at] == BYTE_LBRACKET)) {
            at += 1usize;
            throw ((at >= length) || (source[at] != BYTE_AT)) error { .code = error_code::invalid_selector, .offset = at };
            at += 1usize;
            predicate condition = predicate {
                .name_start = len(bytes), .name_len = 0usize, .has_value = false,
                .value_start = 0usize, .value_len = 0usize,
            };
            while ((at < length) && (is_name_byte(source[at]) == true)) {
                push_byte(&bytes, source[at]);
                condition.name_len += 1usize;
                at += 1usize;
            }
            throw (condition.name_len == 0usize) error { .code = error_code::invalid_selector, .offset = at };
            if ((at < length) && (source[at] == BYTE_EQUAL)) {
                at += 1usize;
                throw ((at >= length) || ((source[at] != BYTE_QUOTE) && (source[at] != BYTE_APOS)))
                    error { .code = error_code::invalid_selector, .offset = at };
                u8 quote = source[at];
                at += 1usize;
                condition.has_value = true;
                condition.value_start = len(bytes);
                while ((at < length) && (source[at] != quote)) {
                    push_byte(&bytes, source[at]);
                    condition.value_len += 1usize;
                    at += 1usize;
                }
                throw (at >= length) error { .code = error_code::invalid_selector, .offset = at };
                at += 1usize;
            }
            throw ((at >= length) || (source[at] != BYTE_RBRACKET)) error { .code = error_code::invalid_selector, .offset = at };
            at += 1usize;
            push_predicate(&predicates, condition);
            current.predicate_count += 1usize;
        }
        push_step(&steps, current);
        if (at < length) {
            throw (source[at] != BYTE_SLASH) error { .code = error_code::invalid_selector, .offset = at };
            throw (current.predicate_count != 0usize) error { .code = error_code::invalid_selector, .offset = at };
            at += 1usize;
            descendant = false;
            if ((at < length) && (source[at] == BYTE_SLASH)) {
                at += 1usize;
                descendant = true;
            }
            throw (at >= length) error { .code = error_code::invalid_selector, .offset = at };
        }
    }
    return selector { .bytes = move bytes, .steps = move steps, .predicates = move predicates, .absolute = absolute };
}

protected bool selector::step_matches(const selector* this, const step* current, const reader* document, usize depth) {
    if (current->any_name == true) { return true; }
    const u8[] bytes = std.array::as_slice(&this->bytes);
    const u8[] wanted = bytes[current->name_start..(current->name_start + current->name_len)];
    str name = document->path_name(depth);
    const u8[] actual = name;
    return same_bytes(wanted, actual);
}

protected bool any_reached(const bool[] reached, usize before) {
    for (usize earlier = 0usize; earlier < before; earlier += 1usize) {
        if (reached[earlier] == true) { return true; }
    }
    return false;
}

/* Whether one attribute predicate holds for the current start_element event. */
protected bool selector::predicate_holds(const selector* this, const predicate* condition, const reader* document) {
    const u8[] bytes = std.array::as_slice(&this->bytes);
    const u8[] wanted_name = bytes[condition->name_start..(condition->name_start + condition->name_len)];
    usize attributes = document->attribute_count();
    for (usize attribute = 0usize; attribute < attributes; attribute += 1usize) {
        str name = document->attribute_name(attribute);
        const u8[] actual_name = name;
        if (same_bytes(wanted_name, actual_name) == true) {
            if (condition->has_value == false) { return true; }
            str value = document->attribute_value(attribute);
            const u8[] actual_value = value;
            const u8[] wanted_value = bytes[condition->value_start..(condition->value_start + condition->value_len)];
            if (same_bytes(wanted_value, actual_value) == true) { return true; }
        }
    }
    return false;
}

/* Advances the reachability of one step over the path. */
protected void selector::advance_step(const selector* this, const step* current, bool first,
                                      const reader* document, usize depth, const bool[] reached, bool[] next) {
    for (usize at = 0usize; at < depth; at += 1usize) {
        bool allowed = false;
        if (first == true) {
            allowed = (this->absolute == false) || (at == 0usize);
        } else {
            if (current->descendant == true) {
                allowed = any_reached(reached, at);
            } else {
                allowed = (at > 0usize) && (reached[at - 1usize] == true);
            }
        }
        next[at] = false;
        if (allowed == true) { next[at] = this->step_matches(current, document, at); }
    }
}

/* True when the current start_element event lies on the selected path. */
bool selector::matches(const selector* this, const reader* document) {
    if (document->kind() != event_kind::start_element) { return false; }
    usize depth = document->depth();
    if ((depth == 0usize) || (depth > SELECTOR_MAX_DEPTH)) { return false; }
    const step[] steps = std.array::as_slice(&this->steps);
    usize count = len(steps);
    if (count == 0usize) { return false; }
    bool[513] reached = {};
    bool[513] next = {};
    for (usize index = 0usize; index < count; index += 1usize) {
        step current = steps[index];
        const bool[] reached_view = &reached;
        bool[] next_view = &next;
        this->advance_step(&current, index == 0usize, document, depth, reached_view, next_view);
        for (usize at = 0usize; at < depth; at += 1usize) { reached[at] = next[at]; }
    }
    if (reached[depth - 1usize] == false) { return false; }
    step last = steps[count - 1usize];
    const predicate[] predicates = std.array::as_slice(&this->predicates);
    for (usize index = 0usize; index < last.predicate_count; index += 1usize) {
        predicate condition = predicates[last.predicate_start + index];
        if (this->predicate_holds(&condition, document) == false) { return false; }
    }
    return true;
}

/* R-SLIB-XML-0006: the writer. Names are written as given; text and attribute values are
   escaped; the pretty form puts each element on its own line, indented by two spaces. */
protected struct open_element {
    usize name_start;
    usize name_len;
    bool has_children;
    bool has_text;
};

struct writer {
    protected array<u8> output;
    protected array<u8> stack_bytes;
    protected array<open_element> frames;
    protected bool pretty;
    protected bool in_start_tag;
};

writer writer::create(bool pretty) {
    array<u8> output = std.array::create::<u8>();
    array<u8> stack_bytes = std.array::create::<u8>();
    array<open_element> frames = std.array::create::<open_element>();
    return writer { .output = move output, .stack_bytes = move stack_bytes, .frames = move frames, .pretty = pretty, .in_start_tag = false };
}

void writer::reset(writer* this) {
    std.array::clear(&this->output);
    std.array::clear(&this->stack_bytes);
    std.array::clear(&this->frames);
    this->in_start_tag = false;
}

usize writer::depth(const writer* this) { return len(this->frames); }
const u8[] writer::as_slice(const writer* this) { return std.array::as_slice(&this->output); }

protected void writer::close_start_tag(writer* this) throws std.alloc::alloc_error {
    if (this->in_start_tag == true) {
        push_byte(&this->output, BYTE_GT);
        this->in_start_tag = false;
    }
}

/* Pretty output breaks lines only inside element-only content: once an element holds text,
   its remaining children stay inline so that mixed content is preserved. */
protected void writer::newline(writer* this, usize depth) throws std.alloc::alloc_error {
    if (this->pretty == false) { return; }
    if (depth != 0usize) {
        const open_element[] frames = std.array::as_slice(&this->frames);
        if (frames[depth - 1usize].has_text == true) { return; }
    }
    if (len(this->output) != 0usize) { push_byte(&this->output, 10); }
    for (usize index = 0usize; index < depth; index += 1usize) {
        push_byte(&this->output, 32);
        push_byte(&this->output, 32);
    }
}

protected void writer::escape(writer* this, str value, bool attribute) throws std.alloc::alloc_error {
    const u8[] bytes = value;
    for (usize index = 0usize; index < len(bytes); index += 1usize) {
        u8 byte = bytes[index];
        if (byte == BYTE_AMP) { push_str(&this->output, "&amp;"); }
        else { if (byte == BYTE_LT) { push_str(&this->output, "&lt;"); }
        else { if (byte == BYTE_GT) { push_str(&this->output, "&gt;"); }
        else { if ((byte == BYTE_QUOTE) && (attribute == true)) { push_str(&this->output, "&quot;"); }
        else { push_byte(&this->output, byte); } } } }
    }
}

protected void writer::mark_children(writer* this) {
    usize depth = len(this->frames);
    if (depth == 0usize) { return; }
    open_element[] frames = std.array::as_slice_mut(&this->frames);
    frames[depth - 1usize].has_children = true;
}

void writer::declaration(writer* this) throws error, std.alloc::alloc_error {
    throw (len(this->output) != 0usize) error { .code = error_code::misplaced, .offset = 0usize };
    push_str(&this->output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
}

void writer::start(writer* this, str name) throws std.alloc::alloc_error {
    this->close_start_tag();
    this->mark_children();
    usize depth = len(this->frames);
    this->newline(depth);
    push_byte(&this->output, BYTE_LT);
    push_str(&this->output, name);
    const u8[] name_bytes = name;
    open_element opened = open_element {
        .name_start = len(this->stack_bytes), .name_len = len(name_bytes), .has_children = false, .has_text = false,
    };
    push_str(&this->stack_bytes, name);
    std.array::reserve(&this->frames, 1usize);
    bool failed = false;
    try { std.array::push(&this->frames, opened); }
    catch (std.array::push_error<open_element> rejected) { failed = true; }
    if (failed == true) { panic("reserved element insertion failed"); }
    this->in_start_tag = true;
}

void writer::attribute(writer* this, str name, str value) throws error, std.alloc::alloc_error {
    throw (this->in_start_tag == false) error { .code = error_code::misplaced, .offset = len(this->output) };
    push_byte(&this->output, 32);
    push_str(&this->output, name);
    push_byte(&this->output, BYTE_EQUAL);
    push_byte(&this->output, BYTE_QUOTE);
    this->escape(value, true);
    push_byte(&this->output, BYTE_QUOTE);
}

void writer::text(writer* this, str value) throws std.alloc::alloc_error {
    this->close_start_tag();
    usize depth = len(this->frames);
    if (depth != 0usize) {
        open_element[] frames = std.array::as_slice_mut(&this->frames);
        frames[depth - 1usize].has_text = true;
    }
    this->escape(value, false);
}

void writer::cdata(writer* this, str value) throws error, std.alloc::alloc_error {
    const u8[] bytes = value;
    for (usize index = 2usize; index < len(bytes); index += 1usize) {
        bool terminator = (bytes[index - 2usize] == BYTE_RBRACKET) && (bytes[index - 1usize] == BYTE_RBRACKET) && (bytes[index] == BYTE_GT);
        throw (terminator == true) error { .code = error_code::malformed, .offset = index };
    }
    this->close_start_tag();
    usize depth = len(this->frames);
    if (depth != 0usize) {
        open_element[] frames = std.array::as_slice_mut(&this->frames);
        frames[depth - 1usize].has_text = true;
    }
    push_str(&this->output, "<![CDATA[");
    push_str(&this->output, value);
    push_str(&this->output, "]]>");
}

void writer::comment(writer* this, str value) throws error, std.alloc::alloc_error {
    const u8[] bytes = value;
    for (usize index = 1usize; index < len(bytes); index += 1usize) {
        bool double_dash = (bytes[index - 1usize] == BYTE_MINUS) && (bytes[index] == BYTE_MINUS);
        throw (double_dash == true) error { .code = error_code::malformed, .offset = index };
    }
    throw ((len(bytes) != 0usize) && (bytes[len(bytes) - 1usize] == BYTE_MINUS)) error { .code = error_code::malformed, .offset = len(bytes) };
    this->close_start_tag();
    this->mark_children();
    usize depth = len(this->frames);
    this->newline(depth);
    push_str(&this->output, "<!--");
    push_str(&this->output, value);
    push_str(&this->output, "-->");
}

void writer::instruction(writer* this, str target, str data) throws error, std.alloc::alloc_error {
    const u8[] target_bytes = target;
    throw (len(target_bytes) == 0usize) error { .code = error_code::malformed, .offset = 0usize };
    const u8[] bytes = data;
    for (usize index = 1usize; index < len(bytes); index += 1usize) {
        bool terminator = (bytes[index - 1usize] == BYTE_QUESTION) && (bytes[index] == BYTE_GT);
        throw (terminator == true) error { .code = error_code::malformed, .offset = index };
    }
    this->close_start_tag();
    this->mark_children();
    usize depth = len(this->frames);
    this->newline(depth);
    push_str(&this->output, "<?");
    push_str(&this->output, target);
    if (len(bytes) != 0usize) {
        push_byte(&this->output, 32);
        push_str(&this->output, data);
    }
    push_str(&this->output, "?>");
}

void writer::end(writer* this) throws error, std.alloc::alloc_error {
    usize depth = len(this->frames);
    throw (depth == 0usize) error { .code = error_code::misplaced, .offset = len(this->output) };
    o<open_element> popped = std.array::pop(&this->frames);
    open_element closing = open_element { .name_start = 0usize, .name_len = 0usize, .has_children = false, .has_text = false };
    switch (popped) {
    case variant o::some(top): closing = *top; break;
    case variant o::none: break;
    }
    if (this->in_start_tag == true) {
        push_str(&this->output, "/>");
        this->in_start_tag = false;
    } else {
        if ((closing.has_children == true) && (closing.has_text == false)) { this->newline(depth - 1usize); }
        push_str(&this->output, "</");
        for (usize index = 0usize; index < closing.name_len; index += 1usize) {
            const u8[] stack = std.array::as_slice(&this->stack_bytes);
            u8 byte = stack[closing.name_start + index];
            push_byte(&this->output, byte);
        }
        push_byte(&this->output, BYTE_GT);
    }
    truncate(&this->stack_bytes, closing.name_start);
}

/* Consumes the writer and returns the document; open elements are an error. */
array<u8> writer::finish(writer this) throws error, std.alloc::alloc_error {
    throw (len(this.frames) != 0usize) error { .code = error_code::unbalanced, .offset = len(this.output) };
    if (this.pretty == true) { push_byte(&this.output, 10); }
    array<u8> output = core::replace(&this.output, std.array::create::<u8>());
    return move output;
}
