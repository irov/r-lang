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

/* File-scope initializers (P4.4-4). The emitter writes the initializer of a static object on one
   line. One that does not fit in 100 columns is written with one element per line and a comma
   after every element, nested lists likewise: clang-format keeps a braced list ending in a comma
   broken after its opening and before its closing brace, with one element per line, so the form
   is a fixed point of it. A designated element whose value is a list puts the list on the lines
   after its designator, as clang-format does. */

typedef struct RC17LayoutSpan {
    size_t start;
    size_t end;
} RC17LayoutSpan;

/* The end of the balanced text from `start` (just after an opening bracket) to its closing
   bracket, or `end` when it is not balanced or holds a literal or comment. */
static size_t r_c17_layout_close(const char *text, size_t start, size_t end) {
    int depth = 1;
    size_t index;

    for (index = start; index < end; ++index) {
        const char value = text[index];
        if ((value == '"') || (value == '\'') ||
            ((value == '/') && (index + 1U < end) &&
             ((text[index + 1U] == '*') || (text[index + 1U] == '/')))) {
            return end;
        }
        if ((value == '{') || (value == '(') || (value == '[')) {
            depth += 1;
        } else if ((value == '}') || (value == ')') || (value == ']')) {
            depth -= 1;
            if (depth == 0) {
                return index;
            }
        }
    }
    return end;
}

static RC17LayoutSpan r_c17_layout_trim(const char *text, size_t start, size_t end) {
    RC17LayoutSpan span;

    while ((start < end) && (text[start] == ' ')) {
        start += 1U;
    }
    while ((end > start) && (text[end - 1U] == ' ')) {
        end -= 1U;
    }
    span.start = start;
    span.end = end;
    return span;
}

static bool r_c17_layout_indent(RC17Emitter *emitter, RC17Buffer *result, size_t indent) {
    static const char spaces[] = "                                ";
    while (indent != 0U) {
        const size_t take = indent < sizeof(spaces) - 1U ? indent : sizeof(spaces) - 1U;
        if (!r_c17_layout_append(emitter, result, spaces, take)) {
            return false;
        }
        indent -= take;
    }
    return true;
}

/* clang-format writes a braced list of 19 or more commas in columns (CommaSeparatedList of
   FormatToken.cpp, for Cpp11BracedListStyle without bin-packed arguments): every column count
   up to 33 whose widest and narrowest item of each column but the last differ by at most 10
   and whose width fits the column limit is a candidate, and of those that fit the remaining
   width it takes the fewest columns that still give the fewest lines. Items count their comma;
   an item is followed by spaces up to the width of its column. Only lists of items without
   nested lists are written so; false when the list has another form. */
#define R_C17_LAYOUT_MAX_COLUMNS 33U

static bool r_c17_layout_columns(RC17Emitter *emitter,
                                 RC17Buffer *result,
                                 const char *text,
                                 const RC17LayoutSpan *items,
                                 size_t count,
                                 size_t indent,
                                 bool *written) {
    size_t sizes[R_C17_LAYOUT_MAX_COLUMNS];
    size_t minimum[R_C17_LAYOUT_MAX_COLUMNS];
    size_t best_columns = 0U;
    size_t best_lines = 0U;
    size_t columns;
    size_t index;

    *written = false;
    if (count < 19U) {
        return true;
    }
    for (columns = R_C17_LAYOUT_MAX_COLUMNS; columns != 0U; --columns) {
        size_t lines = 1U;
        size_t column = 0U;
        size_t width;
        bool full_row = false;
        bool spread = false;

        for (index = 0U; index < columns; ++index) {
            sizes[index] = 0U;
            minimum[index] = SIZE_MAX;
        }
        for (index = 0U; index < count; ++index) {
            const size_t length = items[index].end - items[index].start + 1U;
            if (column == columns) {
                lines += 1U;
                column = 0U;
            }
            if (column == columns - 1U) {
                full_row = true;
            }
            sizes[column] = sizes[column] > length ? sizes[column] : length;
            minimum[column] = minimum[column] < length ? minimum[column] : length;
            column += 1U;
        }
        if (!full_row) {
            continue;
        }
        width = columns - 1U;
        for (index = 0U; index < columns; ++index) {
            width += sizes[index];
        }
        for (index = 0U; index + 1U < columns; ++index) {
            spread = spread || (sizes[index] - minimum[index] > 10U);
        }
        if (spread || ((columns > 1U) && ((width > 100U) || (width > 100U - indent)))) {
            continue;
        }
        if ((best_columns != 0U) && (lines > best_lines)) {
            break;
        }
        best_columns = columns;
        best_lines = lines;
    }
    if (best_columns == 0U) {
        return true;
    }
    /* The widths of the chosen layout. */
    for (index = 0U; index < best_columns; ++index) {
        sizes[index] = 0U;
    }
    for (index = 0U; index < count; ++index) {
        const size_t length = items[index].end - items[index].start + 1U;
        sizes[index % best_columns] =
            sizes[index % best_columns] > length ? sizes[index % best_columns] : length;
    }
    for (index = 0U; index < count; ++index) {
        const size_t column = index % best_columns;
        const size_t length = items[index].end - items[index].start + 1U;
        const bool last = (column == best_columns - 1U) || (index + 1U == count);

        if ((column == 0U) && !r_c17_layout_indent(emitter, result, indent)) {
            return false;
        }
        if (!r_c17_layout_append(
                emitter, result, text + items[index].start, items[index].end - items[index].start) ||
            !r_c17_layout_append(emitter, result, ",", 1U)) {
            return false;
        }
        if (last) {
            if (!r_c17_layout_append(emitter, result, "\n", 1U)) {
                return false;
            }
        } else if (!r_c17_layout_indent(emitter, result, sizes[column] - length + 1U)) {
            return false;
        }
    }
    *written = true;
    return true;
}

/* Writes the elements of the braced list text[start, end) (inside its braces) at `indent`. */
static bool r_c17_layout_list(RC17Emitter *emitter,
                              RC17Buffer *result,
                              const char *text,
                              size_t start,
                              size_t end,
                              size_t indent,
                              unsigned depth) {
    size_t index = start;

    if (depth > 64U) {
        return r_c17_fail(emitter, R_FRONTEND_LIMIT_EXCEEDED);
    }
    {
        /* A long list of items without nested lists may take columns. */
        RC17LayoutSpan *items;
        size_t count = 0U;
        size_t capacity = 1U;
        size_t scan;
        bool scalar = true;
        bool written = false;

        for (scan = start; scan < end; ++scan) {
            capacity += text[scan] == ',' ? 1U : 0U;
            scalar = scalar && (text[scan] != '{');
        }
        if (scalar && (capacity >= 19U)) {
            items = r_c17_allocate(emitter, capacity * sizeof(*items));
            if (items == NULL) {
                return false;
            }
            scan = start;
            while ((scan < end) && (count < capacity)) {
                size_t item_end = scan;
                int nesting = 0;
                while (item_end < end) {
                    const char value = text[item_end];
                    if ((value == '(') || (value == '[')) {
                        nesting += 1;
                    } else if ((value == ')') || (value == ']')) {
                        nesting -= 1;
                    } else if ((value == ',') && (nesting == 0)) {
                        break;
                    }
                    item_end += 1U;
                }
                items[count] = r_c17_layout_trim(text, scan, item_end);
                if (items[count].start != items[count].end) {
                    count += 1U;
                }
                scan = item_end + 1U;
            }
            if (!r_c17_layout_columns(emitter, result, text, items, count, indent, &written)) {
                r_c17_free(emitter, items);
                return false;
            }
            r_c17_free(emitter, items);
            if (written) {
                return true;
            }
        }
    }
    while (index < end) {
        size_t element_end = index;
        RC17LayoutSpan element;
        size_t equals = SIZE_MAX;
        int nesting = 0;

        while (element_end < end) {
            const char value = text[element_end];
            if ((value == '{') || (value == '(') || (value == '[')) {
                nesting += 1;
            } else if ((value == '}') || (value == ')') || (value == ']')) {
                nesting -= 1;
            } else if ((value == ',') && (nesting == 0)) {
                break;
            } else if ((value == '=') && (nesting == 0) && (equals == SIZE_MAX)) {
                equals = element_end;
            }
            element_end += 1U;
        }
        element = r_c17_layout_trim(text, index, element_end);
        index = element_end + 1U;
        if (element.start == element.end) {
            continue;
        }
        if (text[element.start] == '{' &&
            r_c17_layout_close(text, element.start + 1U, element.end) == element.end - 1U) {
            if (!r_c17_layout_indent(emitter, result, indent) ||
                !r_c17_layout_append(emitter, result, "{\n", 2U) ||
                !r_c17_layout_list(emitter,
                                   result,
                                   text,
                                   element.start + 1U,
                                   element.end - 1U,
                                   indent + 4U,
                                   depth + 1U) ||
                !r_c17_layout_indent(emitter, result, indent) ||
                !r_c17_layout_append(emitter, result, "},\n", 3U)) {
                return false;
            }
            continue;
        }
        if (equals != SIZE_MAX) {
            const RC17LayoutSpan value = r_c17_layout_trim(text, equals + 1U, element.end);
            const RC17LayoutSpan designator = r_c17_layout_trim(text, element.start, equals);

            if ((value.start < value.end) && (text[value.start] == '{') &&
                (r_c17_layout_close(text, value.start + 1U, value.end) == value.end - 1U)) {
                if (!r_c17_layout_indent(emitter, result, indent) ||
                    !r_c17_layout_append(emitter,
                                         result,
                                         text + designator.start,
                                         designator.end - designator.start) ||
                    !r_c17_layout_append(emitter, result, " =\n", 3U) ||
                    !r_c17_layout_indent(emitter, result, indent + 4U) ||
                    !r_c17_layout_append(emitter, result, "{\n", 2U) ||
                    !r_c17_layout_list(emitter,
                                       result,
                                       text,
                                       value.start + 1U,
                                       value.end - 1U,
                                       indent + 8U,
                                       depth + 1U) ||
                    !r_c17_layout_indent(emitter, result, indent + 4U) ||
                    !r_c17_layout_append(emitter, result, "},\n", 3U)) {
                    return false;
                }
                continue;
            }
        }
        if (!r_c17_layout_indent(emitter, result, indent) ||
            !r_c17_layout_append(
                emitter, result, text + element.start, element.end - element.start) ||
            !r_c17_layout_append(emitter, result, ",\n", 2U)) {
            return false;
        }
    }
    return true;
}

/* Lays out the file-scope declaration bytes[start, end) that ends in `};`. False only when
   emission must fail; a declaration outside the modeled form is kept as written. */
static bool r_c17_layout_initializer(RC17Emitter *emitter,
                                     RC17Buffer *result,
                                     size_t start,
                                     size_t end) {
    const char *bytes = emitter->output.bytes;
    char *flat;
    size_t flat_length = 0U;
    size_t index = start;
    size_t open = SIZE_MAX;
    size_t close;
    bool success;

    flat = r_c17_allocate(emitter, end - start + 1U);
    if (flat == NULL) {
        return false;
    }
    while (index < end) {
        size_t line_end = index;
        RC17LayoutSpan line;
        while ((line_end < end) && (bytes[line_end] != '\n')) {
            line_end += 1U;
        }
        line = r_c17_layout_trim(bytes, index, line_end);
        if ((flat_length != 0U) && (line.end > line.start) && (flat[flat_length - 1U] != '{') &&
            (bytes[line.start] != '}')) {
            flat[flat_length] = ' ';
            flat_length += 1U;
        }
        (void)memcpy(flat + flat_length, bytes + line.start, line.end - line.start);
        flat_length += line.end - line.start;
        index = line_end + 1U;
    }
    for (index = 0U; index + 3U < flat_length; ++index) {
        if (memcmp(flat + index, " = {", 4U) == 0) {
            open = index + 3U;
            break;
        }
    }
    close = open == SIZE_MAX ? flat_length : r_c17_layout_close(flat, open + 1U, flat_length);
    if ((open == SIZE_MAX) && (flat_length > 100U) && (flat[flat_length - 1U] == ';')) {
        /* An initializer without braces moves after its `=`, indented by four, when it then
           fits: the cheapest break clang-format has for a declaration (2 for the assignment). */
        size_t equals = SIZE_MAX;
        for (index = 0U; index + 2U < flat_length; ++index) {
            if (memcmp(flat + index, " = ", 3U) == 0) {
                equals = index;
                break;
            }
        }
        if ((equals != SIZE_MAX) && (equals + 2U <= 100U) &&
            (4U + flat_length - equals - 3U <= 100U)) {
            success = r_c17_layout_append(emitter, result, flat, equals + 2U) &&
                      r_c17_layout_append(emitter, result, "\n    ", 5U) &&
                      r_c17_layout_append(
                          emitter, result, flat + equals + 3U, flat_length - equals - 3U) &&
                      r_c17_layout_append(emitter, result, "\n", 1U);
            r_c17_free(emitter, flat);
            return success;
        }
    }
    if ((open == SIZE_MAX) || (close + 2U != flat_length) || (flat[close + 1U] != ';')) {
        /* Not a modeled form: kept as written, with the line feed that ends it. */
        success = r_c17_layout_append(emitter,
                                      result,
                                      bytes + start,
                                      end < emitter->output.length ? end - start + 1U
                                                                   : end - start);
    } else if (flat_length <= 100U) {
        success = r_c17_layout_append(emitter, result, flat, flat_length) &&
                  r_c17_layout_append(emitter, result, "\n", 1U);
    } else {
        success = r_c17_layout_append(emitter, result, flat, open + 1U) &&
                  r_c17_layout_append(emitter, result, "\n", 1U) &&
                  r_c17_layout_list(emitter, result, flat, open + 1U, close, 4U, 0U) &&
                  r_c17_layout_append(emitter, result, "};\n", 3U);
    }
    r_c17_free(emitter, flat);
    return success;
}

/* File-scope declarations of functions and of pointers to functions (P4.4-4). clang-format
   puts the parameters of a declaration all on one line or one per line (BinPackParameters:
   false) and chooses among the layouts below by the penalties of its optimizing line formatter.
   The decision procedure was measured against clang-format 22.1.8 over thousands of generated
   declarations of every shape the emitter writes:
     - a function declaration: breaking before the name costs 80 and is not allowed after a
       return type shorter than six columns ("void", "_Bool"), breaking after the opening
       parenthesis costs 140, each break between parameters 41;
     - a declaration that clang-format reads as an object initialized by a call, because no
       parameter names a type (TokenAnnotator's isFunctionDeclarationName: no `const`, pointer,
       parameter name or known type word): the name moves to the next line indented by four for
       74, a break after the parenthesis costs 155, each break between arguments 51;
     - a typedef of a pointer to a function: 140 after the parenthesis, 41 between parameters.
   Among the layouts that fit in 100 columns the cheapest wins, the first listed on a tie. */

enum {
    R_C17_LAYOUT_DECLARATION_FUNCTION,
    R_C17_LAYOUT_DECLARATION_OBJECT,
    R_C17_LAYOUT_DECLARATION_POINTER_TYPEDEF
};

enum {
    R_C17_LAYOUT_ONE,
    R_C17_LAYOUT_OWN,
    R_C17_LAYOUT_AFTER,
    R_C17_LAYOUT_ALIGNED,
    R_C17_LAYOUT_OWN_ALIGNED,
    R_C17_LAYOUT_OWN_AFTER,
    R_C17_LAYOUT_AFTER_ALIGNED,
    R_C17_LAYOUT_SHAPES
};

#define R_C17_LAYOUT_MAX_PARAMETERS 256U

static bool r_c17_layout_word_in(const char *text, size_t length, const char *const *words) {
    for (size_t index = 0U; words[index] != NULL; ++index) {
        if ((strlen(words[index]) == length) && (memcmp(words[index], text, length) == 0)) {
            return true;
        }
    }
    return false;
}

/* Whether clang-format takes a parameter for one of a function declaration. */
static bool r_c17_layout_parameter_is_typed(const char *text, size_t start, size_t end) {
    static const char *const type_words[] = {
        "const",     "void",     "char",     "short",    "int",      "long",     "float",
        "double",    "signed",   "unsigned", "bool",     "char8_t",  "char16_t", "char32_t",
        "clock_t",   "int16_t",  "int32_t",  "int64_t",  "int8_t",   "intptr_t", "ptrdiff_t",
        "size_t",    "time_t",   "uint16_t", "uint32_t", "uint64_t", "uint8_t",  "uintptr_t",
        NULL,
    };
    size_t index = start;
    size_t words = 0U;

    while (index < end) {
        if ((text[index] == '*') || (text[index] == '&') || (text[index] == '.')) {
            return true;
        }
        if ((text[index] == '_') || ((text[index] >= 'a') && (text[index] <= 'z')) ||
            ((text[index] >= 'A') && (text[index] <= 'Z'))) {
            size_t word_end = index;
            while ((word_end < end) &&
                   ((text[word_end] == '_') || ((text[word_end] >= 'a') && (text[word_end] <= 'z')) ||
                    ((text[word_end] >= 'A') && (text[word_end] <= 'Z')) ||
                    ((text[word_end] >= '0') && (text[word_end] <= '9')))) {
                word_end += 1U;
            }
            if (r_c17_layout_word_in(text + index, word_end - index, type_words)) {
                return true;
            }
            words += 1U;
            index = word_end;
            continue;
        }
        index += 1U;
    }
    return words >= 2U;
}

/* Writes one layout of `head name(parameters)tail` into result; head ends where the name starts
   and `opening` is the length of `head name(`. */
static bool r_c17_layout_write_declaration(RC17Emitter *emitter,
                                           RC17Buffer *result,
                                           const char *flat,
                                           size_t head_length,
                                           size_t opening,
                                           const RC17LayoutSpan *parameters,
                                           size_t count,
                                           const char *tail,
                                           unsigned shape,
                                           size_t own_indent) {
    const bool own = (shape == R_C17_LAYOUT_OWN) || (shape == R_C17_LAYOUT_OWN_ALIGNED) ||
                     (shape == R_C17_LAYOUT_OWN_AFTER);
    const bool after = (shape == R_C17_LAYOUT_AFTER) || (shape == R_C17_LAYOUT_OWN_AFTER) ||
                       (shape == R_C17_LAYOUT_AFTER_ALIGNED);
    const bool aligned = (shape == R_C17_LAYOUT_ALIGNED) || (shape == R_C17_LAYOUT_OWN_ALIGNED) ||
                         (shape == R_C17_LAYOUT_AFTER_ALIGNED);
    size_t column;
    size_t index;
    size_t head_end = head_length;

    /* The head without the space before the name when the name moves. */
    while (own && (head_end != 0U) && (flat[head_end - 1U] == ' ')) {
        head_end -= 1U;
    }
    if (!r_c17_layout_append(emitter, result, flat, own ? head_end : head_length)) {
        return false;
    }
    if (own) {
        if (!r_c17_layout_append(emitter, result, "\n", 1U) ||
            !r_c17_layout_indent(emitter, result, own_indent)) {
            return false;
        }
    }
    if (!r_c17_layout_append(emitter, result, flat + head_length, opening - head_length)) {
        return false;
    }
    column = (own ? own_indent : head_length) + (opening - head_length);
    if (after) {
        column = (own ? own_indent : 0U) + 4U;
        if (!r_c17_layout_append(emitter, result, "\n", 1U) ||
            !r_c17_layout_indent(emitter, result, column)) {
            return false;
        }
    }
    for (index = 0U; index < count; ++index) {
        if (index != 0U) {
            if (aligned) {
                if (!r_c17_layout_append(emitter, result, ",\n", 2U) ||
                    !r_c17_layout_indent(emitter, result, column)) {
                    return false;
                }
            } else if (!r_c17_layout_append(emitter, result, ", ", 2U)) {
                return false;
            }
        }
        if (!r_c17_layout_append(emitter,
                                 result,
                                 flat + parameters[index].start,
                                 parameters[index].end - parameters[index].start)) {
            return false;
        }
    }
    return r_c17_layout_append(emitter, result, ")", 1U) &&
           r_c17_layout_append(emitter, result, tail, strlen(tail)) &&
           r_c17_layout_append(emitter, result, "\n", 1U);
}

/* The widest line of one layout, as r_c17_layout_write_declaration would write it. */
static size_t r_c17_layout_declaration_width(const RC17LayoutSpan *parameters,
                                             size_t count,
                                             size_t head_length,
                                             size_t head_trimmed,
                                             size_t opening,
                                             size_t tail_length,
                                             unsigned shape,
                                             size_t own_indent) {
    const bool own = (shape == R_C17_LAYOUT_OWN) || (shape == R_C17_LAYOUT_OWN_ALIGNED) ||
                     (shape == R_C17_LAYOUT_OWN_AFTER);
    const bool after = (shape == R_C17_LAYOUT_AFTER) || (shape == R_C17_LAYOUT_OWN_AFTER) ||
                       (shape == R_C17_LAYOUT_AFTER_ALIGNED);
    const bool aligned = (shape == R_C17_LAYOUT_ALIGNED) || (shape == R_C17_LAYOUT_OWN_ALIGNED) ||
                         (shape == R_C17_LAYOUT_AFTER_ALIGNED);
    size_t widest = own ? head_trimmed : 0U;
    size_t column = (own ? own_indent : head_length) + (opening - head_length);
    size_t line = column;
    size_t index;

    if (after) {
        widest = widest > column ? widest : column;
        column = (own ? own_indent : 0U) + 4U;
        line = column;
    }
    for (index = 0U; index < count; ++index) {
        const size_t length = parameters[index].end - parameters[index].start;
        if (index != 0U) {
            if (aligned) {
                line += 1U;
                widest = widest > line ? widest : line;
                line = column;
            } else {
                line += 2U;
            }
        }
        line += length;
    }
    line += 1U + tail_length;
    return widest > line ? widest : line;
}

static bool r_c17_layout_declaration(RC17Emitter *emitter,
                                     RC17Buffer *result,
                                     size_t start,
                                     size_t end) {
    const char *bytes = emitter->output.bytes;
    RC17LayoutSpan parameters[R_C17_LAYOUT_MAX_PARAMETERS];
    char *flat;
    size_t flat_length = 0U;
    size_t index = start;
    size_t open = SIZE_MAX;
    size_t close;
    size_t name_start;
    size_t head_trimmed;
    size_t count = 0U;
    const char *tail;
    unsigned kind = R_C17_LAYOUT_DECLARATION_FUNCTION;
    unsigned best = R_C17_LAYOUT_SHAPES;
    unsigned long best_penalty = 0UL;
    bool success;

    flat = r_c17_allocate(emitter, end - start + 1U);
    if (flat == NULL) {
        return false;
    }
    while (index < end) {
        size_t line_end = index;
        RC17LayoutSpan line;
        while ((line_end < end) && (bytes[line_end] != '\n')) {
            line_end += 1U;
        }
        line = r_c17_layout_trim(bytes, index, line_end);
        if ((flat_length != 0U) && (line.end > line.start) && (flat[flat_length - 1U] != '(') &&
            (bytes[line.start] != ')')) {
            flat[flat_length] = ' ';
            flat_length += 1U;
        }
        (void)memcpy(flat + flat_length, bytes + line.start, line.end - line.start);
        flat_length += line.end - line.start;
        index = line_end + 1U;
    }
    /* The parameter list: the last top-level parenthesis pair of a pointer typedef
       `typedef T (*name)(...)`, the first one otherwise. */
    if ((flat_length > 8U) && (memcmp(flat, "typedef ", 8U) == 0)) {
        for (index = 8U; index + 1U < flat_length; ++index) {
            if ((flat[index] == '(') && (flat[index + 1U] == '*')) {
                const size_t inner = r_c17_layout_close(flat, index + 1U, flat_length);
                if ((inner + 1U < flat_length) && (flat[inner + 1U] == '(')) {
                    open = inner + 1U;
                    kind = R_C17_LAYOUT_DECLARATION_POINTER_TYPEDEF;
                }
                break;
            }
            if (flat[index] == '(') {
                break;
            }
        }
    }
    if (open == SIZE_MAX) {
        for (index = 0U; index < flat_length; ++index) {
            if ((flat[index] == '(') || (flat[index] == '=')) {
                break;
            }
        }
        open = (index < flat_length) && (flat[index] == '(') ? index : SIZE_MAX;
    }
    close = open == SIZE_MAX ? flat_length : r_c17_layout_close(flat, open + 1U, flat_length);
    tail = (close < flat_length) && (flat_length - close - 1U == 1U) && (flat[close + 1U] == ';')
               ? ";"
           : (close < flat_length) && (flat_length - close - 1U == 2U) &&
                   (memcmp(flat + close + 1U, " {", 2U) == 0)
               ? " {"
               : NULL;
    name_start = open;
    while ((name_start != 0U) && (name_start != SIZE_MAX) &&
           ((flat[name_start - 1U] == '_') ||
            ((flat[name_start - 1U] >= 'a') && (flat[name_start - 1U] <= 'z')) ||
            ((flat[name_start - 1U] >= 'A') && (flat[name_start - 1U] <= 'Z')) ||
            ((flat[name_start - 1U] >= '0') && (flat[name_start - 1U] <= '9')))) {
        name_start -= 1U;
    }
    if (kind == R_C17_LAYOUT_DECLARATION_POINTER_TYPEDEF) {
        name_start = open;
    }
    success = (tail != NULL) && (open != SIZE_MAX) && (name_start != 0U) &&
              ((kind == R_C17_LAYOUT_DECLARATION_POINTER_TYPEDEF) ||
               (name_start < open));
    /* The parameters, split at top-level commas; a parameter with parentheses is not modeled. */
    for (index = open + 1U; success && (index < close);) {
        size_t parameter_end = index;
        while ((parameter_end < close) && (flat[parameter_end] != ',')) {
            if ((flat[parameter_end] == '(') || (flat[parameter_end] == '[') ||
                (flat[parameter_end] == '"') || (flat[parameter_end] == '/')) {
                success = false;
            }
            parameter_end += 1U;
        }
        if (count == R_C17_LAYOUT_MAX_PARAMETERS) {
            success = false;
            break;
        }
        parameters[count] = r_c17_layout_trim(flat, index, parameter_end);
        count += parameters[count].start != parameters[count].end ? 1U : 0U;
        index = parameter_end + 1U;
    }
    if (success && (kind == R_C17_LAYOUT_DECLARATION_FUNCTION) && (tail[0] == ';') && (count != 0U)) {
        bool typed = false;
        for (index = 0U; index < count; ++index) {
            typed = typed ||
                    r_c17_layout_parameter_is_typed(flat, parameters[index].start, parameters[index].end);
        }
        kind = typed ? R_C17_LAYOUT_DECLARATION_FUNCTION : R_C17_LAYOUT_DECLARATION_OBJECT;
    }
    if (!success) {
        /* Not a modeled form: kept as written. */
        success = r_c17_layout_append(emitter,
                                      result,
                                      bytes + start,
                                      end < emitter->output.length ? end - start + 1U
                                                                   : end - start);
        r_c17_free(emitter, flat);
        return success;
    }
    if ((count == 0U) || (flat_length <= 100U)) {
        success = r_c17_layout_append(emitter, result, flat, flat_length) &&
                  r_c17_layout_append(emitter, result, "\n", 1U);
        r_c17_free(emitter, flat);
        return success;
    }
    head_trimmed = name_start;
    while ((head_trimmed != 0U) && (flat[head_trimmed - 1U] == ' ')) {
        head_trimmed -= 1U;
    }
    for (unsigned shape = R_C17_LAYOUT_ONE; shape < R_C17_LAYOUT_SHAPES; ++shape) {
        const bool own = (shape == R_C17_LAYOUT_OWN) || (shape == R_C17_LAYOUT_OWN_ALIGNED) ||
                         (shape == R_C17_LAYOUT_OWN_AFTER);
        const unsigned long own_cost = kind == R_C17_LAYOUT_DECLARATION_OBJECT ? 74UL : 80UL;
        const unsigned long after_cost = kind == R_C17_LAYOUT_DECLARATION_OBJECT ? 155UL : 140UL;
        const unsigned long comma_cost = kind == R_C17_LAYOUT_DECLARATION_OBJECT ? 51UL : 41UL;
        const size_t own_indent = kind == R_C17_LAYOUT_DECLARATION_OBJECT ? 4U : 0U;
        unsigned long penalty = 0UL;

        if (own && ((kind == R_C17_LAYOUT_DECLARATION_POINTER_TYPEDEF) ||
                    ((kind == R_C17_LAYOUT_DECLARATION_FUNCTION) && (head_trimmed < 6U)))) {
            continue;
        }
        switch (shape) {
        case R_C17_LAYOUT_OWN:
            penalty = own_cost;
            break;
        case R_C17_LAYOUT_AFTER:
            penalty = after_cost;
            break;
        case R_C17_LAYOUT_ALIGNED:
            penalty = comma_cost * (unsigned long)(count - 1U);
            break;
        case R_C17_LAYOUT_OWN_ALIGNED:
            penalty = own_cost + comma_cost * (unsigned long)(count - 1U);
            break;
        case R_C17_LAYOUT_OWN_AFTER:
            penalty = own_cost + after_cost;
            break;
        case R_C17_LAYOUT_AFTER_ALIGNED:
            penalty = after_cost + comma_cost * (unsigned long)count;
            break;
        default:
            break;
        }
        if (r_c17_layout_declaration_width(parameters,
                                           count,
                                           name_start,
                                           head_trimmed,
                                           open + 1U,
                                           strlen(tail),
                                           shape,
                                           own_indent) > 100U) {
            continue;
        }
        if ((best == R_C17_LAYOUT_SHAPES) || (penalty < best_penalty)) {
            best = shape;
            best_penalty = penalty;
        }
    }
    if (best == R_C17_LAYOUT_SHAPES) {
        success = r_c17_layout_append(emitter,
                                      result,
                                      bytes + start,
                                      end < emitter->output.length ? end - start + 1U
                                                                   : end - start);
    } else {
        success = r_c17_layout_write_declaration(emitter,
                                                 result,
                                                 flat,
                                                 name_start,
                                                 open + 1U,
                                                 parameters,
                                                 count,
                                                 tail,
                                                 best,
                                                 kind == R_C17_LAYOUT_DECLARATION_OBJECT ? 4U : 0U);
    }
    r_c17_free(emitter, flat);
    return success;
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
        if (!in_unit && (indent == 0U) && (last > content) &&
            ((memcmp(bytes + content, "static ", 7U) == 0) ||
             (memcmp(bytes + content, "const ", 6U) == 0))) {
            /* A file-scope declaration with an initializer runs to the line ending in `};`. */
            size_t scan = content;
            size_t declaration_end = line_end;
            bool initializer = false;
            bool braced = false;

            while (scan < line_end) {
                if ((scan + 3U < line_end) && (memcmp(bytes + scan, " = {", 4U) == 0)) {
                    initializer = true;
                    braced = true;
                    break;
                }
                if ((scan + 2U < line_end) && (memcmp(bytes + scan, " = ", 3U) == 0) &&
                    (bytes[last - 1U] == ';') && (last - content > 100U)) {
                    /* A long initializer without braces on one line. */
                    initializer = true;
                    break;
                }
                if ((bytes[scan] == '(') || (bytes[scan] == ';')) {
                    break;
                }
                scan += 1U;
            }
            if (initializer) {
                while (braced && (declaration_end < length) &&
                       !((declaration_end >= 2U) && (bytes[declaration_end - 1U] == ';') &&
                         (bytes[declaration_end - 2U] == '}'))) {
                    declaration_end += 1U;
                    while ((declaration_end < length) && (bytes[declaration_end] != '\n')) {
                        declaration_end += 1U;
                    }
                }
                if (!r_c17_layout_initializer(emitter, &result, index, declaration_end)) {
                    r_c17_free(emitter, result.bytes);
                    return false;
                }
                index = declaration_end + 1U;
                continue;
            }
        }
        if (!in_unit && (indent == 0U) && (last > content) &&
            ((bytes[content] == '_') || ((bytes[content] >= 'a') && (bytes[content] <= 'z')) ||
             ((bytes[content] >= 'A') && (bytes[content] <= 'Z'))) &&
            (memcmp(bytes + content, "struct ", 7U) != 0) &&
            (memcmp(bytes + content, "union ", 6U) != 0) &&
            (memcmp(bytes + content, "enum ", 5U) != 0) &&
            ((last - content < 15U) || ((memcmp(bytes + content, "typedef struct ", 15U) != 0) &&
                                        (memcmp(bytes + content, "typedef union ", 14U) != 0) &&
                                        (memcmp(bytes + content, "typedef enum ", 13U) != 0)))) {
            /* A file-scope declaration with parameters runs to the line that closes them and
               ends in `;` or `{`. */
            size_t scan = content;
            size_t breaks = 0U;
            bool parenthesized = false;

            static const char *const statements[] = {
                "if", "for", "while", "switch", "return", "case", "default", "goto", "do", "else",
                NULL,
            };
            size_t word = content;

            while ((word < last) &&
                   ((bytes[word] == '_') || ((bytes[word] >= 'a') && (bytes[word] <= 'z')) ||
                    ((bytes[word] >= 'A') && (bytes[word] <= 'Z')) ||
                    ((bytes[word] >= '0') && (bytes[word] <= '9')))) {
                word += 1U;
            }
            if (((word < last) && (bytes[word] == ':')) ||
                r_c17_layout_word_in(bytes + content, word - content, statements)) {
                /* A label or a statement, not a declaration. */
                breaks = 2U;
            }
            /* The return type may stand on a line of its own before the name. */
            while ((scan < length) && (breaks < 2U)) {
                if ((bytes[scan] == '=') || (bytes[scan] == ';') || (bytes[scan] == '{') ||
                    (bytes[scan] == '}') || (bytes[scan] == '#')) {
                    break;
                }
                if (bytes[scan] == '(') {
                    parenthesized = true;
                    break;
                }
                breaks += bytes[scan] == '\n' ? 1U : 0U;
                scan += 1U;
            }
            if (parenthesized) {
                size_t declaration_end = line_end;
                int nesting = 0;
                size_t cursor = content;
                bool complete = false;

                while (cursor < length) {
                    const char value = bytes[cursor];
                    if (value == '(') {
                        nesting += 1;
                    } else if (value == ')') {
                        nesting -= 1;
                    } else if ((value == '\n') && (nesting == 0) && (cursor > 0U) &&
                               ((bytes[cursor - 1U] == ';') || (bytes[cursor - 1U] == '{'))) {
                        declaration_end = cursor;
                        complete = true;
                        break;
                    } else if ((value == '"') || (value == '/')) {
                        break;
                    }
                    cursor += 1U;
                }
                if (complete) {
                    if (!r_c17_layout_declaration(emitter, &result, index, declaration_end)) {
                        r_c17_free(emitter, result.bytes);
                        return false;
                    }
                    index = declaration_end + 1U;
                    continue;
                }
            }
        }
        if (!in_unit && (indent >= 4U) && (line_end - index > 100U) && (last - content > 9U) &&
            (memcmp(bytes + content, "case ", 5U) == 0) && (bytes[last - 1U] == '{') &&
            (bytes[last - 2U] == ' ') && (bytes[last - 3U] == ':') && (bytes[last - 4U] == ')')) {
            /* P4.4-4: a case label past the column limit at a deep nesting breaks inside the
               parentheses of its value, the continuation indented by four, as clang-format
               does even when the continuation does not fit either. */
            size_t open = content + 5U;
            while ((open < last) && (bytes[open] != '(') && (bytes[open] != ' ')) {
                open += 1U;
            }
            if ((open < last) && (bytes[open] == '(')) {
                if (!r_c17_layout_append(emitter, &result, bytes + index, open + 1U - index) ||
                    !r_c17_layout_append(emitter, &result, "\n", 1U) ||
                    !r_c17_layout_indent(emitter, &result, indent + 4U) ||
                    !r_c17_layout_append(emitter, &result, bytes + open + 1U, last - open - 1U) ||
                    !r_c17_layout_append(emitter, &result, "\n", 1U)) {
                    r_c17_free(emitter, result.bytes);
                    return false;
                }
                index = line_end + 1U;
                continue;
            }
        }
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
