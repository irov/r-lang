/* Statement layout pass over the emitted program.

   The emitter writes statements with local wrapping heuristics that hold at the nesting
   depths of ordinary code. This pass runs once over the finished program: every statement
   that spans several lines or exceeds the column limit is flattened and laid out again by the
   clang-format model of layout.inc, so the generated file stays a fixed point of clang-format
   at any nesting depth. Statements the model does not cover (string literals, comments) are
   kept as written. */

static void *r_c17_layout_allocate(void *context, size_t size) {
    return r_c17_allocate((RC17Emitter *)context, size);
}

static void r_c17_layout_release(void *context, void *block) {
    r_c17_free((RC17Emitter *)context, block);
}

static bool
r_c17_layout_append(RC17Emitter *emitter, RC17Buffer *buffer, const char *bytes, size_t length) {
    if (buffer->length + length > buffer->capacity) {
        size_t capacity = buffer->capacity == 0U ? 4096U : buffer->capacity;
        char *replacement;
        while (capacity < buffer->length + length) {
            capacity *= 2U;
        }
        replacement = r_c17_allocate(emitter, capacity);
        if (replacement == NULL) {
            return false;
        }
        if (buffer->length != 0U) {
            (void)memcpy(replacement, buffer->bytes, buffer->length);
        }
        r_c17_free(emitter, buffer->bytes);
        buffer->bytes = replacement;
        buffer->capacity = capacity;
    }
    if (length != 0U) {
        (void)memcpy(buffer->bytes + buffer->length, bytes, length);
        buffer->length += length;
    }
    return true;
}

static bool r_c17_layout_word_is(const char *text, size_t length, const char *word) {
    size_t word_length = strlen(word);
    return (length == word_length) && (memcmp(text, word, word_length) == 0);
}

/* A control header the layout model covers: a condition or loop header ending in the brace
   of its block, or the 'while' of a do loop. */
static bool r_c17_layout_starts_control(const char *text, size_t length) {
    static const char *const headers[] = {
        "if (",
        "while (",
        "switch (",
        "for (",
        "} else if (",
        "} else {",
        "} while (",
    };
    size_t index;
    for (index = 0; index < sizeof(headers) / sizeof(headers[0]); ++index) {
        size_t header_length = strlen(headers[index]);
        if ((length >= header_length) && (memcmp(text, headers[index], header_length) == 0)) {
            return true;
        }
    }
    return false;
}

/* A statement line begins with an identifier, a parenthesis or a dereference and is not a
   control statement, a label, a declaration keyword or a preprocessor line. */
static bool r_c17_layout_starts_statement(const char *text, size_t length) {
    static const char *const excluded[] = {
        "if",      "while", "for",   "switch",   "do",      "else",   "case",
        "default", "goto",  "break", "continue", "typedef", "static", "extern",
        "struct",  "union", "enum",  "asm",      "sizeof",
    };
    size_t word = 0U;
    size_t index;
    char first;

    if (length == 0U) {
        return false;
    }
    first = text[0];
    if (!(((first >= 'a') && (first <= 'z')) || ((first >= 'A') && (first <= 'Z')) ||
          (first == '_') || (first == '(') || (first == '*'))) {
        return false;
    }
    while ((word < length) &&
           (((text[word] >= 'a') && (text[word] <= 'z')) ||
            ((text[word] >= 'A') && (text[word] <= 'Z')) ||
            ((text[word] >= '0') && (text[word] <= '9')) || (text[word] == '_'))) {
        word += 1U;
    }
    for (index = 0; index < sizeof(excluded) / sizeof(excluded[0]); ++index) {
        if (r_c17_layout_word_is(text, word, excluded[index])) {
            return false;
        }
    }
    if ((word < length) && (text[word] == ':') &&
        ((word + 1U >= length) || (text[word + 1U] != ':'))) {
        return false;
    }
    return true;
}

/* Scans one line for bracket balance and unsupported content; literals are skipped. */
static void r_c17_layout_scan_line(const char *text, size_t length, int *depth, bool *supported) {
    size_t index = 0U;
    while (index < length) {
        char value = text[index];
        if ((value == '"') || (value == '\'')) {
            *supported = false;
            index += 1U;
            while ((index < length) && (text[index] != value)) {
                index += (text[index] == '\\') ? 2U : 1U;
            }
            index += 1U;
            continue;
        }
        if ((value == '/') && (index + 1U < length) &&
            ((text[index + 1U] == '*') || (text[index + 1U] == '/'))) {
            *supported = false;
            return;
        }
        if ((value == '(') || (value == '[') || (value == '{')) {
            *depth += 1;
        } else if ((value == ')') || (value == ']') || (value == '}')) {
            *depth -= 1;
        }
        index += 1U;
    }
}

static bool r_c17_layout_unit(RC17Emitter *emitter,
                              RC17Buffer *result,
                              size_t start,
                              size_t end,
                              size_t indent,
                              bool relayout) {
    const char *bytes = emitter->output.bytes;
    char *flat;
    size_t flat_length = 0U;
    size_t index = start;
    size_t capacity;
    char *laid;
    size_t laid_length = 0U;
    RLayoutRequest request;
    RLayoutStatus status;
    bool appended;

    if (!relayout) {
        return r_c17_layout_append(emitter, result, bytes + start, end - start);
    }
    flat = r_c17_allocate(emitter, end - start + 1U);
    if (flat == NULL) {
        return false;
    }
    while (index < end) {
        size_t line_end = index;
        size_t line_start;
        while ((line_end < end) && (bytes[line_end] != '\n')) {
            line_end += 1U;
        }
        line_start = index;
        while ((line_start < line_end) && (bytes[line_start] == ' ')) {
            line_start += 1U;
        }
        while ((line_end > line_start) && (bytes[line_end - 1U] == ' ')) {
            line_end -= 1U;
        }
        if ((flat_length != 0U) && (line_end > line_start)) {
            flat[flat_length] = ' ';
            flat_length += 1U;
        }
        (void)memcpy(flat + flat_length, bytes + line_start, line_end - line_start);
        flat_length += line_end - line_start;
        index = line_end;
        while ((index < end) && (bytes[index] == '\n')) {
            index += 1U;
        }
    }
    capacity = flat_length + (size_t)R_LAYOUT_MAX_TOKENS * (indent + 128U) + 64U;
    laid = r_c17_allocate(emitter, capacity);
    if (laid == NULL) {
        r_c17_free(emitter, flat);
        return false;
    }
    request.text = flat;
    request.length = flat_length;
    request.indent = (unsigned)indent;
    request.allocate = r_c17_layout_allocate;
    request.release = r_c17_layout_release;
    request.context = emitter;
    status = r_layout_statement(&request, laid, capacity, &laid_length);
    if (status == R_LAYOUT_NO_MEMORY) {
        r_c17_free(emitter, laid);
        r_c17_free(emitter, flat);
        return false;
    }
    if (status == R_LAYOUT_DONE) {
        appended = r_c17_layout_append(emitter, result, laid, laid_length) &&
                   r_c17_layout_append(emitter, result, "\n", 1U);
    } else {
        appended = r_c17_layout_append(emitter, result, bytes + start, end - start);
    }
    r_c17_free(emitter, laid);
    r_c17_free(emitter, flat);
    return appended;
}

static bool r_c17_layout_program(RC17Emitter *emitter) {
    const char *bytes = emitter->output.bytes;
    size_t length = emitter->output.length;
    RC17Buffer result;
    size_t index = 0U;
    size_t unit_start = 0U;
    size_t unit_indent = 0U;
    size_t unit_lines = 0U;
    bool in_unit = false;
    bool unit_supported = true;
    bool unit_statement = false;
    bool unit_control = false;
    bool unit_overflows = false;
    int depth = 0;

    (void)memset(&result, 0, sizeof(result));
    while (index < length) {
        size_t line_end = index;
        size_t content = index;
        size_t last;
        size_t indent;
        char tail;

        while ((line_end < length) && (bytes[line_end] != '\n')) {
            line_end += 1U;
        }
        while ((content < line_end) && (bytes[content] == ' ')) {
            content += 1U;
        }
        indent = content - index;
        last = line_end;
        while ((last > content) && (bytes[last - 1U] == ' ')) {
            last -= 1U;
        }
        tail = (last > content) ? bytes[last - 1U] : '\0';
        if (!in_unit) {
            if ((indent < 4U) || (content == line_end) || (bytes[content] == '#')) {
                size_t take = (line_end < length) ? line_end + 1U - index : line_end - index;
                if (!r_c17_layout_append(emitter, &result, bytes + index, take)) {
                    r_c17_free(emitter, result.bytes);
                    return false;
                }
                index = line_end + 1U;
                continue;
            }
            in_unit = true;
            unit_start = index;
            unit_indent = indent;
            unit_lines = 0U;
            unit_supported = true;
            unit_overflows = false;
            depth = 0;
            unit_statement = r_c17_layout_starts_statement(bytes + content, last - content);
            unit_control = r_c17_layout_starts_control(bytes + content, last - content);
        }
        {
            /* A control unit ends at the brace that opens its block; a statement unit only
               ends at its semicolon, so braces of compound literals keep counting. */
            size_t scan_start = content;
            size_t scan_end = last;
            if (!unit_statement) {
                while ((scan_start < scan_end) && (bytes[scan_start] == '}')) {
                    scan_start += 1U;
                }
                if ((scan_end > scan_start) && (tail == '{')) {
                    scan_end -= 1U;
                }
            }
            r_c17_layout_scan_line(
                bytes + scan_start, scan_end - scan_start, &depth, &unit_supported);
        }
        unit_lines += 1U;
        if (line_end - index > 100U) {
            unit_overflows = true;
        }
        if ((depth <= 0) && ((tail == ';') || (tail == '{') || (tail == '}') || (tail == ':') ||
                             (content == line_end))) {
            bool complete = (unit_statement && (tail == ';')) ||
                            (unit_control && ((tail == '{') || (tail == ';')));
            bool relayout =
                unit_supported && complete && (depth == 0) && ((unit_lines > 1U) || unit_overflows);
            size_t unit_end = (line_end < length) ? line_end + 1U : line_end;
            if (!r_c17_layout_unit(emitter, &result, unit_start, unit_end, unit_indent, relayout)) {
                r_c17_free(emitter, result.bytes);
                return false;
            }
            in_unit = false;
        }
        index = line_end + 1U;
    }
    if (in_unit &&
        !r_c17_layout_append(emitter, &result, bytes + unit_start, length - unit_start)) {
        r_c17_free(emitter, result.bytes);
        return false;
    }
    r_c17_free(emitter, emitter->output.bytes);
    emitter->output = result;
    return true;
}
