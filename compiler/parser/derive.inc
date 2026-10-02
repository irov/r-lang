/* R-AGG-0012 (L27): derived implementations. `@derive(clone, equal, ordered, key, format)` before
 * a module-scope struct, enum or error declaration adds the listed capabilities. `equal` and
 * `ordered` implement std.cmp::Equal and std.cmp::Ordered, `key` defines the hash and equal
 * hooks of R-FUNC-0009, `format` implements core::Format (L32), and `clone` makes the structural
 * clone of R-OWN-0020 available. The implementations of equal, ordered, key and format are
 * ordinary declarations generated from the tokens of the declaration and appended after the
 * authored text of its source, one region per capability; they are parsed, checked and translated like written declarations, and a
 * diagnostic inside a region is reported at the capability name. A generic type adds the
 * capability to each of its type parameters, so that an instance has it exactly when its
 * arguments do. */

enum {
    R_DERIVE_CLONE = 1U << 0U,
    R_DERIVE_EQUAL = 1U << 1U,
    R_DERIVE_ORDERED = 1U << 2U,
    R_DERIVE_KEY = 1U << 3U,
    R_DERIVE_FORMAT = 1U << 4U,
};

enum { R_DERIVE_CAPABILITY_COUNT = 5 };

enum { R_DERIVE_SHAPE_NONE = 0, R_DERIVE_SHAPE_PAYLOAD, R_DERIVE_SHAPE_RECORD };

/* A generated or copied name: a range of the declaration's text buffer. */
typedef struct RDeriveName {
    size_t offset;
    size_t length;
} RDeriveName;

typedef struct RDeriveVariant {
    RDeriveName name;
    uint8_t shape;
    size_t first_field;
    size_t field_count;
} RDeriveVariant;

typedef struct RDeriveParameter {
    RDeriveName name;
    RDeriveName text;
    bool is_constant;
    bool is_constrained;
} RDeriveParameter;

typedef struct RDeriveDeclaration {
    RTextBuilder text;
    RDeriveName type_name;
    RDeriveName *fields;
    size_t field_count;
    size_t field_capacity;
    RDeriveVariant *variants;
    size_t variant_count;
    size_t variant_capacity;
    RDeriveParameter *parameters;
    size_t parameter_count;
    size_t parameter_capacity;
    uint32_t capabilities;
    RSourceSpan origins[R_DERIVE_CAPABILITY_COUNT];
    bool is_enum;
    bool is_generic;
    bool has_pack;
    bool valid;
    /* The fields of a parent error could not be read; its generated declarations are omitted
       and the semantic check reports why. */
    bool incomplete;
} RDeriveDeclaration;

static const char *const r_derive_capability_names[R_DERIVE_CAPABILITY_COUNT] = {
    "clone", "equal", "ordered", "key", "format"};

static size_t r_derive_next(const RSource *source, size_t index) {
    return r_skip_trivia(source, index + 1U);
}

static bool r_derive_token_is(const RSource *source, size_t index, const char *text) {
    const RToken *token = r_scan_token(source, index);
    return (token != NULL) && (token->kind == R_TOKEN_IDENTIFIER) &&
           r_source_text_equal(source, token->span, text);
}

static void r_derive_free(RFrontendContext *context, RDeriveDeclaration *declaration) {
    r_context_free(context, declaration->text.items);
    r_context_free(context, declaration->fields);
    r_context_free(context, declaration->variants);
    r_context_free(context, declaration->parameters);
    (void)memset(declaration, 0, sizeof(*declaration));
}

/* Copies the text of the significant tokens [first, last] into the declaration's buffer,
   separated by single spaces where the source separates them. */
static bool r_derive_copy_tokens(RFrontendContext *context,
                                 RDeriveDeclaration *declaration,
                                 const RSource *source,
                                 size_t first,
                                 size_t last,
                                 RDeriveName *name) {
    size_t index = r_skip_trivia(source, first);
    name->offset = declaration->text.count;
    while ((index <= last) && (index < source->token_count)) {
        const RToken *token = &source->tokens[index];
        if ((index != r_skip_trivia(source, first)) && (index > 0U) &&
            r_token_is_trivia(source->tokens[index - 1U].kind) &&
            !r_text_builder_append(context, &declaration->text, " ", 1U)) {
            return false;
        }
        if (!r_text_builder_append_token(context, &declaration->text, source, token)) {
            return false;
        }
        index = r_derive_next(source, index);
    }
    name->length = declaration->text.count - name->offset;
    return true;
}

static bool r_derive_add_field(RFrontendContext *context,
                               RDeriveDeclaration *declaration,
                               const RSource *source,
                               size_t token) {
    RDeriveName name;
    if (!r_derive_copy_tokens(context, declaration, source, token, token, &name) ||
        !r_grow_array(context,
                      (void **)&declaration->fields,
                      &declaration->field_capacity,
                      sizeof(*declaration->fields),
                      declaration->field_count + 1U)) {
        return false;
    }
    declaration->fields[declaration->field_count++] = name;
    return true;
}

/* The index of the token that closes the group opened at `open`, or the end of the tokens. */
static size_t r_derive_close(const RSource *source, size_t open) {
    const RTokenKind opening = r_scan_kind(source, open);
    size_t depth = 0U;
    size_t index = open;
    while (index < source->token_count) {
        const RTokenKind kind = r_scan_kind(source, index);
        if (kind == R_TOKEN_EOF) {
            return index;
        }
        if (opening == R_TOKEN_LESS) {
            if (kind == R_TOKEN_LESS) {
                depth += 1U;
            } else if (kind == R_TOKEN_GREATER) {
                depth -= 1U;
            } else if (kind == R_TOKEN_GREATER_GREATER) {
                depth = depth >= 2U ? depth - 2U : 0U;
            }
        } else if ((kind == R_TOKEN_LPAREN) || (kind == R_TOKEN_LBRACE) ||
                   (kind == R_TOKEN_LBRACKET)) {
            depth += 1U;
        } else if ((kind == R_TOKEN_RPAREN) || (kind == R_TOKEN_RBRACE) ||
                   (kind == R_TOKEN_RBRACKET)) {
            depth -= 1U;
        }
        if (depth == 0U) {
            return index;
        }
        index = r_derive_next(source, index);
    }
    return index;
}

/* The fields of a struct or error body opened at `open`: each declaration ends at `;`, and its
   name is the last identifier before `;` or before an `=` outside brackets. */
static bool r_derive_read_fields(RFrontendContext *context,
                                 RDeriveDeclaration *declaration,
                                 const RSource *source,
                                 size_t open,
                                 size_t *close) {
    size_t index = r_derive_next(source, open);
    while ((r_scan_kind(source, index) != R_TOKEN_RBRACE) &&
           (r_scan_kind(source, index) != R_TOKEN_EOF)) {
        size_t name = SIZE_MAX;
        size_t depth = 0U;
        bool initializer = false;
        while (r_scan_kind(source, index) != R_TOKEN_EOF) {
            const RTokenKind kind = r_scan_kind(source, index);
            if ((depth == 0U) && ((kind == R_TOKEN_SEMICOLON) || (kind == R_TOKEN_RBRACE))) {
                break;
            }
            if ((kind == R_TOKEN_LPAREN) || (kind == R_TOKEN_LBRACKET) ||
                (kind == R_TOKEN_LBRACE)) {
                depth += 1U;
            } else if ((kind == R_TOKEN_RPAREN) || (kind == R_TOKEN_RBRACKET) ||
                       (kind == R_TOKEN_RBRACE)) {
                depth -= 1U;
            } else if ((depth == 0U) && (kind == R_TOKEN_EQUAL)) {
                initializer = true;
            } else if ((depth == 0U) && !initializer && (kind == R_TOKEN_IDENTIFIER)) {
                name = index;
            }
            index = r_derive_next(source, index);
        }
        if ((name != SIZE_MAX) && !r_derive_add_field(context, declaration, source, name)) {
            return false;
        }
        if (r_scan_kind(source, index) == R_TOKEN_SEMICOLON) {
            index = r_derive_next(source, index);
        }
    }
    *close = index;
    return true;
}

/* The variants of an enum body opened at `open`. */
static bool r_derive_read_variants(RFrontendContext *context,
                                   RDeriveDeclaration *declaration,
                                   const RSource *source,
                                   size_t open) {
    /* A variant may follow its attributes (`@default`, L33). */
    size_t index =
        r_skip_trivia(source, r_scan_skip_leading_attributes(source, r_derive_next(source, open)));
    while (r_scan_kind(source, index) == R_TOKEN_IDENTIFIER) {
        RDeriveVariant variant;
        size_t after = r_derive_next(source, index);
        (void)memset(&variant, 0, sizeof(variant));
        if (!r_derive_copy_tokens(context, declaration, source, index, index, &variant.name)) {
            return false;
        }
        variant.first_field = declaration->field_count;
        if (r_scan_kind(source, after) == R_TOKEN_LPAREN) {
            variant.shape = R_DERIVE_SHAPE_PAYLOAD;
            after = r_derive_next(source, r_derive_close(source, after));
        } else if (r_scan_kind(source, after) == R_TOKEN_LBRACE) {
            size_t close;
            variant.shape = R_DERIVE_SHAPE_RECORD;
            if (!r_derive_read_fields(context, declaration, source, after, &close)) {
                return false;
            }
            after = r_derive_next(source, close);
        } else if (r_scan_kind(source, after) == R_TOKEN_EQUAL) {
            size_t depth = 0U;
            while (r_scan_kind(source, after) != R_TOKEN_EOF) {
                const RTokenKind kind = r_scan_kind(source, after);
                if ((depth == 0U) && ((kind == R_TOKEN_COMMA) || (kind == R_TOKEN_RBRACE))) {
                    break;
                }
                if ((kind == R_TOKEN_LPAREN) || (kind == R_TOKEN_LBRACKET)) {
                    depth += 1U;
                } else if (((kind == R_TOKEN_RPAREN) || (kind == R_TOKEN_RBRACKET)) &&
                           (depth != 0U)) {
                    depth -= 1U;
                }
                after = r_derive_next(source, after);
            }
        }
        variant.field_count = declaration->field_count - variant.first_field;
        if (!r_grow_array(context,
                          (void **)&declaration->variants,
                          &declaration->variant_capacity,
                          sizeof(*declaration->variants),
                          declaration->variant_count + 1U)) {
            return false;
        }
        declaration->variants[declaration->variant_count++] = variant;
        if (r_scan_kind(source, after) != R_TOKEN_COMMA) {
            break;
        }
        index = r_skip_trivia(source,
                              r_scan_skip_leading_attributes(source, r_derive_next(source, after)));
    }
    return true;
}

/* The parameters of `@generic<...>` between the angle brackets at `open` and `close`. */
static bool r_derive_read_parameters(RFrontendContext *context,
                                     RDeriveDeclaration *declaration,
                                     const RSource *source,
                                     size_t open,
                                     size_t close) {
    size_t index = r_derive_next(source, open);
    declaration->is_generic = true;
    while (index < close) {
        RDeriveParameter parameter;
        size_t last = index;
        size_t depth = 0U;
        size_t cursor = index;
        (void)memset(&parameter, 0, sizeof(parameter));
        parameter.is_constant = r_scan_kind(source, index) == R_TOKEN_KW_CONST;
        while (cursor < close) {
            const RTokenKind kind = r_scan_kind(source, cursor);
            if ((depth == 0U) && (kind == R_TOKEN_COMMA)) {
                break;
            }
            if ((kind == R_TOKEN_LESS) || (kind == R_TOKEN_LPAREN) ||
                (kind == R_TOKEN_LBRACKET)) {
                depth += 1U;
            } else if (((kind == R_TOKEN_GREATER) || (kind == R_TOKEN_RPAREN) ||
                        (kind == R_TOKEN_RBRACKET)) &&
                       (depth != 0U)) {
                depth -= 1U;
            } else if (kind == R_TOKEN_GREATER_GREATER) {
                depth = depth >= 2U ? depth - 2U : 0U;
            } else if ((depth == 0U) && (kind == R_TOKEN_COLON)) {
                parameter.is_constrained = true;
            } else if ((depth == 0U) && (kind == R_TOKEN_ELLIPSIS)) {
                declaration->has_pack = true;
            }
            last = cursor;
            cursor = r_derive_next(source, cursor);
        }
        {
            /* A type parameter is named first; a constant parameter `const T N` last. */
            size_t name = index;
            if (parameter.is_constant) {
                size_t scan = index;
                while (scan <= last) {
                    if (r_scan_kind(source, scan) == R_TOKEN_IDENTIFIER) {
                        name = scan;
                    }
                    scan = r_derive_next(source, scan);
                }
            }
            if (!r_derive_copy_tokens(context, declaration, source, name, name, &parameter.name) ||
                !r_derive_copy_tokens(context, declaration, source, index, last, &parameter.text) ||
                !r_grow_array(context,
                              (void **)&declaration->parameters,
                              &declaration->parameter_capacity,
                              sizeof(*declaration->parameters),
                              declaration->parameter_count + 1U)) {
                return false;
            }
        }
        declaration->parameters[declaration->parameter_count++] = parameter;
        index = r_derive_next(source, cursor);
    }
    return true;
}

static bool r_derive_read_error_base(RFrontendContext *context,
                                     RDeriveDeclaration *declaration,
                                     const RSource *source,
                                     size_t colon,
                                     size_t open,
                                     uint32_t depth);

/* The fields an error inherits from the parent named by the tokens after `colon`, parent
   fields first (R-AGG-0011). */
static bool r_derive_read_error_fields(RFrontendContext *context,
                                       RDeriveDeclaration *declaration,
                                       const RSource *source,
                                       size_t name,
                                       uint32_t depth) {
    size_t after = r_derive_next(source, name);
    size_t open = after;
    size_t close;
    if (depth > 32U) {
        declaration->valid = false;
        return true;
    }
    if (r_scan_kind(source, after) == R_TOKEN_COLON) {
        open = r_derive_next(source, after);
        while ((r_scan_kind(source, open) != R_TOKEN_LBRACE) &&
               (r_scan_kind(source, open) != R_TOKEN_EOF)) {
            open = r_derive_next(source, open);
        }
        if (!r_derive_read_error_base(context, declaration, source, after, open, depth)) {
            return false;
        }
    }
    if (r_scan_kind(source, open) != R_TOKEN_LBRACE) {
        declaration->valid = false;
        return true;
    }
    return r_derive_read_fields(context, declaration, source, open, &close);
}

static bool r_derive_read_error_base(RFrontendContext *context,
                                     RDeriveDeclaration *declaration,
                                     const RSource *source,
                                     size_t colon,
                                     size_t open,
                                     uint32_t depth) {
    RTextBuilder module = {0};
    const RToken *base = NULL;
    const RSourceId source_id = (RSourceId)((source - context->sources) + 1);
    size_t index = r_derive_next(source, colon);
    bool qualified = false;
    bool success = true;
    /* `Parent` or `a.b::Parent`: the module path precedes `::`. */
    while (index < open) {
        const RToken *token = r_scan_token(source, index);
        if (token == NULL) {
            break;
        }
        if (token->kind == R_TOKEN_COLON_COLON) {
            if ((base != NULL) && !qualified &&
                !r_text_builder_append(context,
                                       &module,
                                       (const char *)(source->bytes + base->span.start),
                                       (size_t)(base->span.end - base->span.start))) {
                success = false;
                break;
            }
            base = NULL;
            qualified = true;
        } else if (qualified || (token->kind == R_TOKEN_IDENTIFIER)) {
            if ((token->kind == R_TOKEN_IDENTIFIER) && (base != NULL) && !qualified &&
                !r_text_builder_append(context,
                                       &module,
                                       (const char *)(source->bytes + base->span.start),
                                       (size_t)(base->span.end - base->span.start))) {
                success = false;
                break;
            }
            if (token->kind == R_TOKEN_IDENTIFIER) {
                base = token;
            }
        } else if ((token->kind == R_TOKEN_DOT) && (base != NULL)) {
            if (!r_text_builder_append(context,
                                       &module,
                                       (const char *)(source->bytes + base->span.start),
                                       (size_t)(base->span.end - base->span.start)) ||
                !r_text_builder_append(context, &module, ".", 1U)) {
                success = false;
                break;
            }
            base = NULL;
        }
        index = r_derive_next(source, index);
    }
    if (success && (base != NULL)) {
        const RAggregateEntry *entry = NULL;
        for (size_t candidate = 0U; (entry == NULL) && (candidate < context->aggregate_count);
             ++candidate) {
            const RAggregateEntry *aggregate = &context->aggregates[candidate];
            bool visible;
            if (!aggregate->is_error || !r_source_text_equal(source, base->span, aggregate->name)) {
                continue;
            }
            if (qualified) {
                visible = (module.items != NULL) &&
                          (strcmp(module.items, aggregate->module_name) == 0);
            } else {
                visible = aggregate->source == source_id;
                for (size_t import = 0U; !visible && (import < context->import_count); ++import) {
                    const RImportEntry *entry_import = &context->imports[import];
                    visible = (entry_import->source == source_id) && entry_import->imports_name &&
                              (strcmp(entry_import->module_name, aggregate->module_name) == 0) &&
                              (strcmp(entry_import->name, aggregate->name) == 0);
                }
            }
            if (visible) {
                entry = aggregate;
            }
        }
        if (entry == NULL) {
            declaration->incomplete = true;
        } else {
            const RSource *declaring = &context->sources[(size_t)entry->source - 1U];
            size_t name = SIZE_MAX;
            for (size_t token = 0U; token < declaring->token_count; ++token) {
                if ((declaring->tokens[token].kind == R_TOKEN_IDENTIFIER) &&
                    (declaring->tokens[token].span.end == entry->declaration_offset)) {
                    name = token;
                    break;
                }
            }
            if (name == SIZE_MAX) {
                declaration->incomplete = true;
            } else {
                success =
                    r_derive_read_error_fields(context, declaration, declaring, name, depth + 1U);
            }
        }
    } else if (base == NULL) {
        declaration->incomplete = true;
    }
    r_context_free(context, module.items);
    return success;
}

/* The attribute run that starts at `at`: its derive and generic attributes and the struct, enum
   or error declaration it precedes. `last` receives the last token of the run. */
static bool r_derive_read(RFrontendContext *context,
                          const RSource *source,
                          size_t at,
                          RDeriveDeclaration *declaration,
                          size_t *last) {
    size_t index = at;
    size_t generic_open = SIZE_MAX;
    size_t generic_close = SIZE_MAX;
    size_t keyword;
    size_t name;
    size_t after;
    *last = at;
    while (r_scan_kind(source, index) == R_TOKEN_AT) {
        const size_t attribute = r_derive_next(source, index);
        size_t following;
        if (r_scan_kind(source, attribute) != R_TOKEN_IDENTIFIER) {
            *last = index;
            return true;
        }
        following = r_derive_next(source, attribute);
        *last = attribute;
        if (r_derive_token_is(source, attribute, "derive") &&
            (r_scan_kind(source, following) == R_TOKEN_LPAREN)) {
            const size_t close = r_derive_close(source, following);
            size_t argument = r_derive_next(source, following);
            while (argument < close) {
                const RToken *token = r_scan_token(source, argument);
                const bool named = (token != NULL) && (token->kind == R_TOKEN_IDENTIFIER);
                for (uint32_t bit = 0U; named && (bit < R_DERIVE_CAPABILITY_COUNT); ++bit) {
                    if (r_source_text_equal(source, token->span, r_derive_capability_names[bit]) &&
                        ((declaration->capabilities & (1U << bit)) == 0U)) {
                        declaration->capabilities |= 1U << bit;
                        declaration->origins[bit] = token->span;
                    }
                }
                argument = r_derive_next(source, argument);
            }
            *last = close;
            index = r_derive_next(source, close);
            continue;
        }
        if (r_derive_token_is(source, attribute, "generic") &&
            (r_scan_kind(source, following) == R_TOKEN_LESS)) {
            generic_open = following;
            generic_close = r_derive_close(source, following);
            *last = generic_close;
            index = r_derive_next(source, generic_close);
            continue;
        }
        if ((r_scan_kind(source, following) == R_TOKEN_LPAREN) ||
            (r_scan_kind(source, following) == R_TOKEN_LESS)) {
            *last = r_derive_close(source, following);
            index = r_derive_next(source, *last);
            continue;
        }
        index = following;
    }
    if (declaration->capabilities == 0U) {
        return true;
    }
    keyword = index;
    if (r_scan_kind(source, keyword) == R_TOKEN_KW_PROTECTED) {
        keyword = r_derive_next(source, keyword);
    }
    name = r_derive_next(source, keyword);
    if (r_scan_kind(source, name) != R_TOKEN_IDENTIFIER) {
        return true;
    }
    after = r_derive_next(source, name);
    declaration->valid = true;
    if (!r_derive_copy_tokens(context, declaration, source, name, name, &declaration->type_name) ||
        ((generic_open != SIZE_MAX) &&
         !r_derive_read_parameters(context, declaration, source, generic_open, generic_close))) {
        return false;
    }
    if (r_scan_kind(source, keyword) == R_TOKEN_KW_STRUCT) {
        size_t close;
        if (r_scan_kind(source, after) != R_TOKEN_LBRACE) {
            declaration->valid = false;
            return true;
        }
        return r_derive_read_fields(context, declaration, source, after, &close);
    }
    if (r_scan_kind(source, keyword) == R_TOKEN_KW_ENUM) {
        declaration->is_enum = true;
        if (r_scan_kind(source, after) == R_TOKEN_COLON) {
            after = r_derive_next(source, r_derive_next(source, after));
        }
        if (r_scan_kind(source, after) != R_TOKEN_LBRACE) {
            declaration->valid = false;
            return true;
        }
        return r_derive_read_variants(context, declaration, source, after);
    }
    if (!r_scan_error_declaration(source, keyword)) {
        declaration->valid = false;
        return true;
    }
    /* R-AGG-0004, R-AGG-0011: an error with an integer representation or with variants is an
       enum; an error with fields, possibly extending a parent, is a struct. */
    if (r_scan_kind(source, after) == R_TOKEN_COLON) {
        if (r_token_is_primitive_type(r_scan_kind(source, r_derive_next(source, after)))) {
            declaration->is_enum = true;
            return r_derive_read_variants(
                context, declaration, source, r_derive_next(source, r_derive_next(source, after)));
        }
        return r_derive_read_error_fields(context, declaration, source, name, 0U);
    }
    if (r_scan_kind(source, after) != R_TOKEN_LBRACE) {
        declaration->valid = false;
        return true;
    }
    {
        const size_t member =
            r_skip_trivia(source, r_scan_skip_leading_attributes(source, r_derive_next(source, after)));
        size_t probe = r_derive_next(source, member);
        if (r_scan_kind(source, member) == R_TOKEN_IDENTIFIER) {
            if (r_scan_kind(source, probe) == R_TOKEN_LPAREN) {
                probe = r_derive_next(source, r_derive_close(source, probe));
            }
            if ((r_scan_kind(source, probe) == R_TOKEN_COMMA) ||
                (r_scan_kind(source, probe) == R_TOKEN_RBRACE) ||
                (r_scan_kind(source, probe) == R_TOKEN_EQUAL) ||
                (r_scan_kind(source, probe) == R_TOKEN_LBRACE)) {
                declaration->is_enum = true;
                return r_derive_read_variants(context, declaration, source, after);
            }
        }
    }
    return r_derive_read_error_fields(context, declaration, source, name, 0U);
}

/* Output helpers over the generated text. */
typedef struct RDeriveWriter {
    RFrontendContext *context;
    RTextBuilder *output;
    const RDeriveDeclaration *declaration;
    const char *prefix;
    bool ok;
} RDeriveWriter;

static void r_derive_write(RDeriveWriter *writer, const char *text) {
    writer->ok = writer->ok &&
                 r_text_builder_append(writer->context, writer->output, text, strlen(text));
}

static void r_derive_write_name(RDeriveWriter *writer, RDeriveName name) {
    writer->ok = writer->ok && r_text_builder_append(writer->context,
                                                     writer->output,
                                                     writer->declaration->text.items + name.offset,
                                                     name.length);
}

/* A generated local or parameter name: the prefix no identifier of the source starts with. */
static void r_derive_write_local(RDeriveWriter *writer, const char *suffix) {
    r_derive_write(writer, writer->prefix);
    r_derive_write(writer, suffix);
}

static void r_derive_write_number(RDeriveWriter *writer, size_t value) {
    char digits[32];
    (void)snprintf(digits, sizeof(digits), "%zu", value);
    r_derive_write(writer, digits);
}

/* The derived type as written in the generated declarations: `Name` or `Name<P1, P2>`. */
static void r_derive_write_type(RDeriveWriter *writer) {
    const RDeriveDeclaration *declaration = writer->declaration;
    r_derive_write_name(writer, declaration->type_name);
    if (declaration->is_generic) {
        r_derive_write(writer, "<");
        for (size_t index = 0U; index < declaration->parameter_count; ++index) {
            if (index != 0U) {
                r_derive_write(writer, ", ");
            }
            r_derive_write_name(writer, declaration->parameters[index].name);
        }
        r_derive_write(writer, ">");
    }
}

/* Whether the constraint list of `parameter` already names `bound` as one of its `&` terms. */
static bool r_derive_has_bound(const RDeriveDeclaration *declaration,
                               const RDeriveParameter *parameter,
                               const char *bound) {
    const char *text = declaration->text.items + parameter->text.offset;
    const size_t length = parameter->text.length;
    const size_t bound_length = strlen(bound);
    size_t index = 0U;
    while ((index < length) && (text[index] != ':')) {
        ++index;
    }
    while (index < length) {
        size_t start = index + 1U;
        size_t end;
        while ((start < length) && (text[start] == ' ')) {
            ++start;
        }
        end = start;
        while ((end < length) && (text[end] != '&')) {
            ++end;
        }
        index = end;
        while ((end > start) && (text[end - 1U] == ' ')) {
            --end;
        }
        if ((end - start == bound_length) && (memcmp(text + start, bound, bound_length) == 0)) {
            return true;
        }
    }
    return false;
}

/* `@generic<...>` of the type with `bound` added to each type parameter, or nothing. */
static void r_derive_write_header(RDeriveWriter *writer, const char *bound) {
    const RDeriveDeclaration *declaration = writer->declaration;
    if (!declaration->is_generic) {
        return;
    }
    r_derive_write(writer, "@generic<");
    for (size_t index = 0U; index < declaration->parameter_count; ++index) {
        const RDeriveParameter *parameter = &declaration->parameters[index];
        if (index != 0U) {
            r_derive_write(writer, ", ");
        }
        r_derive_write_name(writer, parameter->text);
        if (parameter->is_constant || r_derive_has_bound(declaration, parameter, bound)) {
            continue;
        }
        r_derive_write(writer, parameter->is_constrained ? " & " : ": ");
        r_derive_write(writer, bound);
    }
    r_derive_write(writer, ">\n");
}

static void r_derive_write_variant_path(RDeriveWriter *writer, const RDeriveVariant *variant) {
    r_derive_write_type(writer);
    r_derive_write(writer, "::");
    r_derive_write_name(writer, variant->name);
}

/* `derived_left->name` style access to the field `field` through the local `local`. */
static void
r_derive_write_member(RDeriveWriter *writer, const char *local, RDeriveName field, bool self) {
    if (self) {
        r_derive_write(writer, "this");
    } else {
        r_derive_write_local(writer, local);
    }
    r_derive_write(writer, "->");
    r_derive_write_name(writer, field);
}

static void r_derive_write_equal(RDeriveWriter *writer) {
    const RDeriveDeclaration *declaration = writer->declaration;
    r_derive_write_header(writer, "std.cmp::Equal");
    r_derive_write(writer, "impl std.cmp::Equal for ");
    r_derive_write_type(writer);
    r_derive_write(writer, " {\n    bool eq(const ");
    r_derive_write_type(writer);
    r_derive_write(writer, "* this, const ");
    r_derive_write_type(writer);
    r_derive_write(writer, "* ");
    r_derive_write_local(writer, "other");
    r_derive_write(writer, ") {\n");
    if (!declaration->is_enum) {
        for (size_t index = 0U; index < declaration->field_count; ++index) {
            r_derive_write(writer, "        if (");
            r_derive_write_member(writer, NULL, declaration->fields[index], true);
            r_derive_write(writer, ".eq(&");
            r_derive_write_member(writer, "other", declaration->fields[index], false);
            r_derive_write(writer, ") == false) { return false; }\n");
        }
        r_derive_write(writer, "        return true;\n    }\n};\n");
        return;
    }
    r_derive_write(writer, "        switch (*this) {\n");
    for (size_t index = 0U; index < declaration->variant_count; ++index) {
        const RDeriveVariant *variant = &declaration->variants[index];
        const bool payload = variant->shape != R_DERIVE_SHAPE_NONE;
        r_derive_write(writer, "        case variant ");
        r_derive_write_variant_path(writer, variant);
        if (payload) {
            r_derive_write(writer, "(");
            r_derive_write_local(writer, "left");
            r_derive_write(writer, ")");
        }
        r_derive_write(writer, ":\n            switch (*");
        r_derive_write_local(writer, "other");
        r_derive_write(writer, ") {\n            case variant ");
        r_derive_write_variant_path(writer, variant);
        if (payload) {
            r_derive_write(writer, "(");
            r_derive_write_local(writer, "right");
            r_derive_write(writer, ")");
        }
        r_derive_write(writer, ":\n");
        if (variant->shape == R_DERIVE_SHAPE_PAYLOAD) {
            r_derive_write(writer, "                return ");
            r_derive_write_local(writer, "left");
            r_derive_write(writer, "->eq(");
            r_derive_write_local(writer, "right");
            r_derive_write(writer, ");\n");
        } else {
            for (size_t field = 0U; field < variant->field_count; ++field) {
                const RDeriveName name = declaration->fields[variant->first_field + field];
                r_derive_write(writer, "                if (");
                r_derive_write_member(writer, "left", name, false);
                r_derive_write(writer, ".eq(&");
                r_derive_write_member(writer, "right", name, false);
                r_derive_write(writer, ") == false) { return false; }\n");
            }
            r_derive_write(writer, "                return true;\n");
        }
        r_derive_write(writer, "            default: return false;\n            }\n");
    }
    r_derive_write(writer, "        }\n        return false;\n    }\n};\n");
}

/* `usize name = match (*value) { case variant T::a(_): 0usize; ... };` */
static void r_derive_write_index(RDeriveWriter *writer, const char *name, const char *value) {
    const RDeriveDeclaration *declaration = writer->declaration;
    r_derive_write(writer, "        usize ");
    r_derive_write_local(writer, name);
    r_derive_write(writer, " = match (*");
    if (strcmp(value, "this") == 0) {
        r_derive_write(writer, "this");
    } else {
        r_derive_write_local(writer, value);
    }
    r_derive_write(writer, ") {\n");
    for (size_t index = 0U; index < declaration->variant_count; ++index) {
        const RDeriveVariant *variant = &declaration->variants[index];
        r_derive_write(writer, "        case variant ");
        r_derive_write_variant_path(writer, variant);
        r_derive_write(writer, variant->shape != R_DERIVE_SHAPE_NONE ? "(_): " : ": ");
        r_derive_write_number(writer, index);
        r_derive_write(writer, "usize;\n");
    }
    r_derive_write(writer, "        };\n");
}

/* Lexicographic comparison of `count` fields read through `left` and `right`. */
static void r_derive_write_field_order(RDeriveWriter *writer,
                                       const RDeriveName *fields,
                                       size_t count,
                                       const char *left,
                                       const char *right,
                                       const char *indent) {
    if (count == 0U) {
        r_derive_write(writer, indent);
        r_derive_write(writer, "return std.cmp::ordering::equal;\n");
        return;
    }
    for (size_t index = 0U; index + 1U < count; ++index) {
        r_derive_write(writer, indent);
        r_derive_write(writer, "std.cmp::ordering ");
        r_derive_write_local(writer, "order_");
        r_derive_write_number(writer, index + 1U);
        r_derive_write(writer, " = ");
        r_derive_write_member(writer, left, fields[index], left == NULL);
        r_derive_write(writer, ".cmp(&");
        r_derive_write_member(writer, right, fields[index], false);
        r_derive_write(writer, ");\n");
        r_derive_write(writer, indent);
        r_derive_write(writer, "if (");
        r_derive_write_local(writer, "order_");
        r_derive_write_number(writer, index + 1U);
        r_derive_write(writer, " != std.cmp::ordering::equal) { return ");
        r_derive_write_local(writer, "order_");
        r_derive_write_number(writer, index + 1U);
        r_derive_write(writer, "; }\n");
    }
    r_derive_write(writer, indent);
    r_derive_write(writer, "return ");
    r_derive_write_member(writer, left, fields[count - 1U], left == NULL);
    r_derive_write(writer, ".cmp(&");
    r_derive_write_member(writer, right, fields[count - 1U], false);
    r_derive_write(writer, ");\n");
}

static void r_derive_write_ordered(RDeriveWriter *writer) {
    const RDeriveDeclaration *declaration = writer->declaration;
    bool payloads = false;
    r_derive_write_header(writer, "std.cmp::Ordered");
    r_derive_write(writer, "impl std.cmp::Ordered for ");
    r_derive_write_type(writer);
    r_derive_write(writer, " {\n    std.cmp::ordering cmp(const ");
    r_derive_write_type(writer);
    r_derive_write(writer, "* this, const ");
    r_derive_write_type(writer);
    r_derive_write(writer, "* ");
    r_derive_write_local(writer, "other");
    r_derive_write(writer, ") {\n");
    if (!declaration->is_enum) {
        r_derive_write_field_order(
            writer, declaration->fields, declaration->field_count, NULL, "other", "        ");
        r_derive_write(writer, "    }\n};\n");
        return;
    }
    r_derive_write_index(writer, "left_index", "this");
    r_derive_write_index(writer, "right_index", "other");
    r_derive_write(writer, "        if (");
    r_derive_write_local(writer, "left_index");
    r_derive_write(writer, " < ");
    r_derive_write_local(writer, "right_index");
    r_derive_write(writer, ") { return std.cmp::ordering::less; }\n        if (");
    r_derive_write_local(writer, "left_index");
    r_derive_write(writer, " > ");
    r_derive_write_local(writer, "right_index");
    r_derive_write(writer, ") { return std.cmp::ordering::greater; }\n");
    for (size_t index = 0U; index < declaration->variant_count; ++index) {
        payloads = payloads || (declaration->variants[index].shape != R_DERIVE_SHAPE_NONE);
    }
    if (payloads) {
        r_derive_write(writer, "        switch (*this) {\n");
        for (size_t index = 0U; index < declaration->variant_count; ++index) {
            const RDeriveVariant *variant = &declaration->variants[index];
            if (variant->shape == R_DERIVE_SHAPE_NONE) {
                continue;
            }
            r_derive_write(writer, "        case variant ");
            r_derive_write_variant_path(writer, variant);
            r_derive_write(writer, "(");
            r_derive_write_local(writer, "left");
            r_derive_write(writer, "):\n            switch (*");
            r_derive_write_local(writer, "other");
            r_derive_write(writer, ") {\n            case variant ");
            r_derive_write_variant_path(writer, variant);
            r_derive_write(writer, "(");
            r_derive_write_local(writer, "right");
            r_derive_write(writer, "):\n");
            if (variant->shape == R_DERIVE_SHAPE_PAYLOAD) {
                r_derive_write(writer, "                return ");
                r_derive_write_local(writer, "left");
                r_derive_write(writer, "->cmp(");
                r_derive_write_local(writer, "right");
                r_derive_write(writer, ");\n");
            } else {
                r_derive_write_field_order(writer,
                                           declaration->fields + variant->first_field,
                                           variant->field_count,
                                           "left",
                                           "right",
                                           "                ");
            }
            r_derive_write(writer, "            default: return std.cmp::ordering::equal;\n");
            r_derive_write(writer, "            }\n");
        }
        r_derive_write(writer, "        default: return std.cmp::ordering::equal;\n        }\n");
    }
    r_derive_write(writer, "        return std.cmp::ordering::equal;\n    }\n};\n");
}

/* One FNV-1a step: `u64 next = (previous ^ term) * 1099511628211u64;`. */
static void r_derive_write_hash_step(RDeriveWriter *writer,
                                     const char *indent,
                                     size_t step,
                                     const char *term_prefix,
                                     RDeriveName field,
                                     bool field_term,
                                     const char *literal) {
    r_derive_write(writer, indent);
    r_derive_write(writer, "u64 ");
    r_derive_write_local(writer, "hash_");
    r_derive_write_number(writer, step);
    r_derive_write(writer, " = (");
    if (step == 0U) {
        r_derive_write(writer, "14695981039346656037u64");
    } else {
        r_derive_write_local(writer, "hash_");
        r_derive_write_number(writer, step - 1U);
    }
    r_derive_write(writer, " ^ ");
    if (literal != NULL) {
        r_derive_write(writer, literal);
    } else {
        r_derive_write(writer, "core::hash(");
        if (field_term) {
            r_derive_write(writer, "&");
            r_derive_write_member(writer, term_prefix, field, false);
        } else {
            r_derive_write_local(writer, term_prefix);
        }
        r_derive_write(writer, ")");
    }
    r_derive_write(writer, ") * 1099511628211u64;\n");
}

static void r_derive_write_key(RDeriveWriter *writer) {
    const RDeriveDeclaration *declaration = writer->declaration;
    const RDeriveName none = {0U, 0U};
    /* The hash hook. */
    r_derive_write_header(writer, "key");
    r_derive_write(writer, "u64 ");
    r_derive_write_type(writer);
    r_derive_write(writer, "::hash(const ");
    r_derive_write_type(writer);
    r_derive_write(writer, "* ");
    r_derive_write_local(writer, "value");
    r_derive_write(writer, ") {\n");
    if (!declaration->is_enum) {
        r_derive_write_hash_step(writer, "    ", 0U, NULL, none, false, "0u64");
        for (size_t index = 0U; index < declaration->field_count; ++index) {
            r_derive_write_hash_step(
                writer, "    ", index + 1U, "value", declaration->fields[index], true, NULL);
        }
        r_derive_write(writer, "    return ");
        r_derive_write_local(writer, "hash_");
        r_derive_write_number(writer, declaration->field_count);
        r_derive_write(writer, ";\n}\n");
    } else {
        r_derive_write(writer, "    switch (*");
        r_derive_write_local(writer, "value");
        r_derive_write(writer, ") {\n");
        for (size_t index = 0U; index < declaration->variant_count; ++index) {
            const RDeriveVariant *variant = &declaration->variants[index];
            char ordinal[32];
            size_t steps = 0U;
            (void)snprintf(ordinal, sizeof(ordinal), "%zuu64", index);
            r_derive_write(writer, "    case variant ");
            r_derive_write_variant_path(writer, variant);
            if (variant->shape != R_DERIVE_SHAPE_NONE) {
                r_derive_write(writer, "(");
                r_derive_write_local(writer, "payload");
                r_derive_write(writer, ")");
            }
            r_derive_write(writer, ": {\n");
            r_derive_write_hash_step(writer, "        ", 0U, NULL, none, false, ordinal);
            if (variant->shape == R_DERIVE_SHAPE_PAYLOAD) {
                r_derive_write_hash_step(writer, "        ", 1U, "payload", none, false, NULL);
                steps = 1U;
            } else if (variant->shape == R_DERIVE_SHAPE_RECORD) {
                for (size_t field = 0U; field < variant->field_count; ++field) {
                    r_derive_write_hash_step(writer,
                                             "        ",
                                             field + 1U,
                                             "payload",
                                             declaration->fields[variant->first_field + field],
                                             true,
                                             NULL);
                }
                steps = variant->field_count;
            }
            r_derive_write(writer, "        return ");
            r_derive_write_local(writer, "hash_");
            r_derive_write_number(writer, steps);
            r_derive_write(writer, ";\n    }\n");
        }
        r_derive_write(writer, "    }\n    return 0u64;\n}\n");
    }
    /* The equal hook. */
    r_derive_write_header(writer, "key");
    r_derive_write(writer, "bool ");
    r_derive_write_type(writer);
    r_derive_write(writer, "::equal(const ");
    r_derive_write_type(writer);
    r_derive_write(writer, "* ");
    r_derive_write_local(writer, "left");
    r_derive_write(writer, ", const ");
    r_derive_write_type(writer);
    r_derive_write(writer, "* ");
    r_derive_write_local(writer, "right");
    r_derive_write(writer, ") {\n");
    if (!declaration->is_enum) {
        for (size_t index = 0U; index < declaration->field_count; ++index) {
            r_derive_write(writer, "    if (core::key_equal(&");
            r_derive_write_member(writer, "left", declaration->fields[index], false);
            r_derive_write(writer, ", &");
            r_derive_write_member(writer, "right", declaration->fields[index], false);
            r_derive_write(writer, ") == false) { return false; }\n");
        }
        r_derive_write(writer, "    return true;\n}\n");
        return;
    }
    r_derive_write(writer, "    switch (*");
    r_derive_write_local(writer, "left");
    r_derive_write(writer, ") {\n");
    for (size_t index = 0U; index < declaration->variant_count; ++index) {
        const RDeriveVariant *variant = &declaration->variants[index];
        const bool payload = variant->shape != R_DERIVE_SHAPE_NONE;
        r_derive_write(writer, "    case variant ");
        r_derive_write_variant_path(writer, variant);
        if (payload) {
            r_derive_write(writer, "(");
            r_derive_write_local(writer, "left_payload");
            r_derive_write(writer, ")");
        }
        r_derive_write(writer, ":\n        switch (*");
        r_derive_write_local(writer, "right");
        r_derive_write(writer, ") {\n        case variant ");
        r_derive_write_variant_path(writer, variant);
        if (payload) {
            r_derive_write(writer, "(");
            r_derive_write_local(writer, "right_payload");
            r_derive_write(writer, ")");
        }
        r_derive_write(writer, ":\n");
        if (variant->shape == R_DERIVE_SHAPE_PAYLOAD) {
            r_derive_write(writer, "            return core::key_equal(");
            r_derive_write_local(writer, "left_payload");
            r_derive_write(writer, ", ");
            r_derive_write_local(writer, "right_payload");
            r_derive_write(writer, ");\n");
        } else {
            for (size_t field = 0U; field < variant->field_count; ++field) {
                const RDeriveName name = declaration->fields[variant->first_field + field];
                r_derive_write(writer, "            if (core::key_equal(&");
                r_derive_write_member(writer, "left_payload", name, false);
                r_derive_write(writer, ", &");
                r_derive_write_member(writer, "right_payload", name, false);
                r_derive_write(writer, ") == false) { return false; }\n");
            }
            r_derive_write(writer, "            return true;\n");
        }
        r_derive_write(writer, "        default: return false;\n        }\n");
    }
    r_derive_write(writer, "    }\n    return false;\n}\n");
}

/* `std.format::append_str(out, "text");` for text without quotes or backslashes. */
static void r_derive_write_text_append(RDeriveWriter *writer, const char *indent) {
    r_derive_write(writer, indent);
    r_derive_write(writer, "std.format::append_str(");
    r_derive_write_local(writer, "out");
    r_derive_write(writer, ", \"");
}

/* `place.format(out);` for the field `field` of `this` or of the payload borrow `local`. */
static void r_derive_write_field_format(RDeriveWriter *writer,
                                        const char *indent,
                                        const char *local,
                                        RDeriveName field) {
    r_derive_write(writer, indent);
    r_derive_write_member(writer, local, field, local == NULL);
    r_derive_write(writer, ".format(");
    r_derive_write_local(writer, "out");
    r_derive_write(writer, ");\n");
}

/* `{ a: x, b: y }` after the name already appended: the fields in declaration order. */
static void r_derive_write_record_format(RDeriveWriter *writer,
                                         const RDeriveName *fields,
                                         size_t count,
                                         const char *local,
                                         const char *indent) {
    if (count == 0U) {
        r_derive_write_text_append(writer, indent);
        r_derive_write(writer, " {}\");\n");
        return;
    }
    for (size_t index = 0U; index < count; ++index) {
        r_derive_write_text_append(writer, indent);
        r_derive_write(writer, index == 0U ? " { " : ", ");
        r_derive_write_name(writer, fields[index]);
        r_derive_write(writer, ": \");\n");
        r_derive_write_field_format(writer, indent, local, fields[index]);
    }
    r_derive_write_text_append(writer, indent);
    r_derive_write(writer, " }\");\n");
}

/* R-AGG-0012 (L32): core::Format writes a struct or error as `Name { field: value, ... }` and a
 * variant as `name`, `name(payload)` or `name { field: value, ... }`. */
static void r_derive_write_format(RDeriveWriter *writer) {
    const RDeriveDeclaration *declaration = writer->declaration;
    r_derive_write_header(writer, "core::Format");
    r_derive_write(writer, "impl core::Format for ");
    r_derive_write_type(writer);
    r_derive_write(writer, " {\n    void format(const ");
    r_derive_write_type(writer);
    r_derive_write(writer, "* this, std.format::builder* ");
    r_derive_write_local(writer, "out");
    r_derive_write(writer, ") throws std.alloc::alloc_error {\n");
    if (!declaration->is_enum) {
        r_derive_write_text_append(writer, "        ");
        r_derive_write_name(writer, declaration->type_name);
        r_derive_write(writer, "\");\n");
        r_derive_write_record_format(
            writer, declaration->fields, declaration->field_count, NULL, "        ");
        r_derive_write(writer, "    }\n};\n");
        return;
    }
    r_derive_write(writer, "        switch (*this) {\n");
    for (size_t index = 0U; index < declaration->variant_count; ++index) {
        const RDeriveVariant *variant = &declaration->variants[index];
        r_derive_write(writer, "        case variant ");
        r_derive_write_variant_path(writer, variant);
        if (variant->shape != R_DERIVE_SHAPE_NONE) {
            r_derive_write(writer, "(");
            r_derive_write_local(writer, "payload");
            r_derive_write(writer, ")");
        }
        r_derive_write(writer, ": {\n");
        r_derive_write_text_append(writer, "            ");
        r_derive_write_name(writer, variant->name);
        r_derive_write(writer, variant->shape == R_DERIVE_SHAPE_PAYLOAD ? "(\");\n" : "\");\n");
        if (variant->shape == R_DERIVE_SHAPE_PAYLOAD) {
            r_derive_write(writer, "            ");
            r_derive_write_local(writer, "payload");
            r_derive_write(writer, "->format(");
            r_derive_write_local(writer, "out");
            r_derive_write(writer, ");\n");
            r_derive_write_text_append(writer, "            ");
            r_derive_write(writer, ")\");\n");
        } else if (variant->shape == R_DERIVE_SHAPE_RECORD) {
            r_derive_write_record_format(writer,
                                         declaration->fields + variant->first_field,
                                         variant->field_count,
                                         "payload",
                                         "            ");
        }
        r_derive_write(writer, "        }\n");
    }
    r_derive_write(writer, "        }\n    }\n};\n");
}

/* The prefix of generated names: `derived_`, or `derivedN_` when an identifier of the source
   already starts with it. */
static bool r_derive_prefix(const RSource *source, char *prefix, size_t capacity) {
    for (uint32_t attempt = 0U; attempt < 1000U; ++attempt) {
        bool taken = false;
        size_t length;
        if (attempt == 0U) {
            (void)snprintf(prefix, capacity, "derived_");
        } else {
            (void)snprintf(prefix, capacity, "derived%u_", (unsigned int)attempt);
        }
        length = strlen(prefix);
        for (size_t index = 0U; !taken && (index < source->token_count); ++index) {
            const RToken *token = &source->tokens[index];
            taken = (token->kind == R_TOKEN_IDENTIFIER) &&
                    ((size_t)(token->span.end - token->span.start) >= length) &&
                    (memcmp(source->bytes + token->span.start, prefix, length) == 0);
        }
        if (!taken) {
            return true;
        }
    }
    return false;
}

static bool r_derive_add_region(RFrontendContext *context,
                                RSource *source,
                                size_t start,
                                size_t end,
                                RSourceSpan origin,
                                bool incomplete) {
    if (!r_grow_array(context,
                      (void **)&source->derive_regions,
                      &source->derive_region_capacity,
                      sizeof(*source->derive_regions),
                      source->derive_region_count + 1U)) {
        return false;
    }
    source->derive_regions[source->derive_region_count].start =
        source->authored_length + (uint32_t)start;
    source->derive_regions[source->derive_region_count].end =
        source->authored_length + (uint32_t)end;
    source->derive_regions[source->derive_region_count].origin = origin;
    source->derive_regions[source->derive_region_count].suppressed = incomplete;
    source->derive_regions[source->derive_region_count].incomplete = incomplete;
    source->derive_region_count += 1U;
    return true;
}

RFrontendStatus r_derive_expand(RFrontendContext *context, RSourceId source_id) {
    RSource *source = r_get_source(context, source_id);
    RTextBuilder output = {0};
    char prefix[32];
    size_t depth = 0U;
    size_t index;
    bool success = true;

    if (source == NULL) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (source->derive_expanded) {
        return R_FRONTEND_OK;
    }
    source->derive_expanded = true;
    source->authored_length = (uint32_t)source->length;
    if (!source->lexed || source->has_lex_error || (source->tokens == NULL) ||
        !r_derive_prefix(source, prefix, sizeof(prefix))) {
        return R_FRONTEND_OK;
    }
    for (index = r_skip_trivia(source, 0U); success && (index < source->token_count);
         index = r_derive_next(source, index)) {
        const RTokenKind kind = source->tokens[index].kind;
        RDeriveDeclaration declaration;
        size_t last = index;
        if (kind == R_TOKEN_EOF) {
            break;
        }
        if (kind == R_TOKEN_LBRACE) {
            depth += 1U;
        } else if ((kind == R_TOKEN_RBRACE) && (depth != 0U)) {
            depth -= 1U;
        }
        if ((depth != 0U) || (kind != R_TOKEN_AT)) {
            continue;
        }
        (void)memset(&declaration, 0, sizeof(declaration));
        success = r_derive_read(context, source, index, &declaration, &last);
        if (success && declaration.valid && !declaration.has_pack) {
            RDeriveWriter writer = {context, &output, &declaration, prefix, true};
            static const uint32_t order[4] = {
                R_DERIVE_EQUAL, R_DERIVE_ORDERED, R_DERIVE_KEY, R_DERIVE_FORMAT};
            for (size_t capability = 0U; writer.ok && capability < 4U; ++capability) {
                const uint32_t bit = order[capability];
                const size_t start = output.count;
                uint32_t slot = 0U;
                if ((declaration.capabilities & bit) == 0U) {
                    continue;
                }
                while ((1U << slot) != bit) {
                    ++slot;
                }
                if (declaration.incomplete) {
                    writer.ok = r_derive_add_region(
                        context, source, start, start, declaration.origins[slot], true);
                    continue;
                }
                r_derive_write(&writer, "\n");
                if (bit == R_DERIVE_EQUAL) {
                    r_derive_write_equal(&writer);
                } else if (bit == R_DERIVE_ORDERED) {
                    r_derive_write_ordered(&writer);
                } else if (bit == R_DERIVE_KEY) {
                    r_derive_write_key(&writer);
                } else {
                    r_derive_write_format(&writer);
                }
                writer.ok = writer.ok && r_derive_add_region(context,
                                                             source,
                                                             start,
                                                             output.count,
                                                             declaration.origins[slot],
                                                             false);
            }
            success = writer.ok;
        }
        r_derive_free(context, &declaration);
        index = last;
    }
    if (success && (output.count != 0U)) {
        success = r_lex_append(context, source_id, output.items, output.count);
    }
    r_context_free(context, output.items);
    if (!success) {
        return context->resource_status != R_FRONTEND_OK ? context->resource_status
                                                         : R_FRONTEND_INTERNAL_ERROR;
    }
    return R_FRONTEND_OK;
}

/* R-AGG-0012 (L27): a module that derives equal or ordered and does not import std.cmp imports
   it, so that the generated implementations name std.cmp::Equal and std.cmp::Ordered. */
static bool r_derive_scan_imports(RFrontendContext *context, RSourceId source_id, RSource *source) {
    size_t index;
    for (index = r_skip_trivia(source, 0U); index < source->token_count;
         index = r_derive_next(source, index)) {
        size_t argument;
        size_t close;
        if ((source->tokens[index].kind != R_TOKEN_AT) ||
            !r_derive_token_is(source, r_derive_next(source, index), "derive") ||
            (r_scan_kind(source, r_derive_next(source, r_derive_next(source, index))) !=
             R_TOKEN_LPAREN)) {
            continue;
        }
        argument = r_derive_next(source, r_derive_next(source, index));
        close = r_derive_close(source, argument);
        for (argument = r_derive_next(source, argument); argument < close;
             argument = r_derive_next(source, argument)) {
            if (r_derive_token_is(source, argument, "equal") ||
                r_derive_token_is(source, argument, "ordered")) {
                const RToken *token = r_scan_token(source, argument);
                for (size_t import = 0U; import < context->import_count; ++import) {
                    if ((context->imports[import].source == source_id) &&
                        !context->imports[import].imports_name &&
                        (strcmp(context->imports[import].module_name, "std.cmp") == 0)) {
                        return true;
                    }
                }
                return r_add_import_entry(context, source_id, "std.cmp", token->span, source, NULL);
            }
        }
        index = close;
    }
    return true;
}
