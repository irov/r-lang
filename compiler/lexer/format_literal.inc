/* Split interpolation before escape decoding: escaped braces are always text. */
static size_t r_format_escape_end(const RSource *source, size_t offset, size_t limit) {
    size_t end = offset + 1U;
    if (end >= limit) {
        return limit;
    }
    if (source->bytes[end] == (uint8_t)'u' && end + 1U < limit &&
        source->bytes[end + 1U] == (uint8_t)'{') {
        end += 2U;
        while (end < limit && source->bytes[end] != (uint8_t)'}') {
            ++end;
        }
        return end < limit ? end + 1U : limit;
    }
    if (source->bytes[end] == (uint8_t)'x') {
        return end + 3U < limit ? end + 3U : limit;
    }
    return end + 1U;
}

static bool r_format_text_token(
    RFrontendContext *context, RSource *source, RSourceId source_id, size_t start, size_t end) {
    RSource quoted = {0};
    RByteVector decoded = {0};
    size_t cursor = start;
    size_t length = 1U;
    size_t decoded_end;
    bool closed;
    bool success = false;
    uint32_t intern_id;
    uint8_t *bytes;
    if (start == end) {
        return true;
    }
    bytes = r_context_allocate(context, end - start + 2U + R_SOURCE_PADDING);
    if (bytes == NULL) {
        return false;
    }
    bytes[0] = (uint8_t)'"';
    while (cursor < end) {
        size_t next = cursor + 1U;
        if (source->bytes[cursor] == (uint8_t)'\\') {
            next = r_format_escape_end(source, cursor, end);
        } else if (next < end &&
                   (source->bytes[cursor] == (uint8_t)'{' ||
                    source->bytes[cursor] == (uint8_t)'}') &&
                   source->bytes[next] == source->bytes[cursor]) {
            ++cursor;
            ++next;
        }
        (void)memcpy(bytes + length, source->bytes + cursor, next - cursor);
        length += next - cursor;
        cursor = next;
    }
    bytes[length++] = (uint8_t)'"';
    (void)memset(bytes + length, 0, R_SOURCE_PADDING);
    quoted.bytes = bytes;
    quoted.length = length;
    if (!r_decode_literal(context,
                          &quoted,
                          source_id,
                          0U,
                          (uint8_t)'"',
                          &decoded,
                          &decoded_end,
                          &closed,
                          false) ||
        !r_intern_bytes(context, decoded.items, decoded.count, &intern_id) ||
        !r_add_token(context, source, R_TOKEN_FORMAT_TEXT, (uint32_t)start, (uint32_t)end)) {
        goto cleanup;
    }
    source->tokens[source->token_count - 1U].intern_id = intern_id;
    success = true;
cleanup:
    r_context_free(context, decoded.items);
    r_context_free(context, bytes);
    return success;
}

static bool r_scan_format_literal(
    RFrontendContext *context, RSource *source, RSourceId source_id, size_t start, size_t *end) {
    RTokenKind ignored;
    size_t cursor = start + 2U;
    size_t text_start = cursor;
    size_t limit;
    bool closed;
    if (!r_scan_literal(context, source, source_id, start + 1U, (uint8_t)'"', end, &ignored) ||
        !r_add_token(context, source, R_TOKEN_FORMAT_START, (uint32_t)start, (uint32_t)cursor)) {
        return false;
    }
    closed = *end > cursor && source->bytes[*end - 1U] == (uint8_t)'"';
    limit = closed ? *end - 1U : *end;
    while (cursor < limit) {
        uint8_t byte = source->bytes[cursor];
        if (byte == (uint8_t)'\\') {
            cursor = r_format_escape_end(source, cursor, limit);
        } else if ((byte == (uint8_t)'{' || byte == (uint8_t)'}') && cursor + 1U < limit &&
                   source->bytes[cursor + 1U] == byte) {
            cursor += 2U;
        } else if (byte == (uint8_t)'{') {
            if (!r_format_text_token(context, source, source_id, text_start, cursor) ||
                !r_add_token(
                    context, source, R_TOKEN_LBRACE, (uint32_t)cursor, (uint32_t)(cursor + 1U))) {
                return false;
            }
            ++cursor;
            while (cursor < limit && source->bytes[cursor] != (uint8_t)'}') {
                size_t next = cursor + 1U;
                RTokenKind kind = R_TOKEN_ERROR;
                RAsciiLexeme lexeme = r_lexer_scan_ascii(source->bytes + cursor);
                if (source->bytes[cursor] == (uint8_t)':') {
                    if (!r_add_token(
                            context, source, R_TOKEN_COLON, (uint32_t)cursor, (uint32_t)next)) {
                        return false;
                    }
                    cursor = next;
                    while (next < limit && source->bytes[next] != (uint8_t)'}') {
                        ++next;
                    }
                    kind = R_TOKEN_FORMAT_SPEC;
                } else if (lexeme.kind == R_ASCII_LEXEME_IDENTIFIER ||
                           lexeme.kind == R_ASCII_LEXEME_NON_ASCII) {
                    if (!r_scan_identifier(context,
                                           source,
                                           source_id,
                                           cursor,
                                           lexeme.length,
                                           lexeme.kind == R_ASCII_LEXEME_NON_ASCII,
                                           &next,
                                           &kind)) {
                        return false;
                    }
                } else if (lexeme.kind == R_ASCII_LEXEME_NUMBER_START) {
                    while (next < limit && source->bytes[next] >= (uint8_t)'0' &&
                           source->bytes[next] <= (uint8_t)'9') {
                        ++next;
                    }
                    kind = R_TOKEN_INTEGER_LITERAL;
                } else if (lexeme.kind == R_ASCII_LEXEME_WHITESPACE) {
                    kind = R_TOKEN_WHITESPACE;
                    next = cursor + lexeme.length;
                } else if (lexeme.kind == R_ASCII_LEXEME_PUNCTUATOR) {
                    kind = lexeme.token_kind;
                    next = cursor + lexeme.length;
                }
                if (next > limit) {
                    next = limit;
                }
                if (!r_add_token(context, source, kind, (uint32_t)cursor, (uint32_t)next)) {
                    return false;
                }
                cursor = next;
            }
            if (cursor < limit) {
                if (!r_add_token(context,
                                 source,
                                 R_TOKEN_RBRACE,
                                 (uint32_t)cursor,
                                 (uint32_t)(cursor + 1U))) {
                    return false;
                }
                ++cursor;
            }
            text_start = cursor;
        } else {
            if (byte == (uint8_t)'}') {
                (void)r_literal_error(context,
                                      source_id,
                                      cursor,
                                      cursor + 1U,
                                      "literal closing brace in formatted text requires '}}'",
                                      true);
            }
            ++cursor;
        }
    }
    return r_format_text_token(context, source, source_id, text_start, limit) &&
           (!closed ||
            r_add_token(context, source, R_TOKEN_FORMAT_END, (uint32_t)limit, (uint32_t)*end));
}
