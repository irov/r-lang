/* B10.2 (R-FFI-0065, R-FFI-0066): the C header and the ABI baseline of a library.
 *
 * The API of a library is NAME_initialize, NAME_shutdown and its exports. The header declares
 * them in C with every R-declared @repr(C) type their signatures reach, defined under its R name
 * with the layout r_semantic_aggregate_layout gives it (packing, alignments, bit-fields) and
 * static assertions of that layout, so a C compiler that lays a definition out otherwise rejects
 * the header; a C-declared type comes from the headers of its extern block, which the header
 * includes. R-FFI-0004 admits no other type in an export's signature: a handle of an R type
 * (R-FFI-0008) passes as void *. The baseline records the same API as data: each export's prototype without parameter
 * names and each R-declared type's kind, size, alignment and members or enumerators, which
 * r_abi_baseline_compare checks a later build of the library against. */

typedef struct RCLibraryApi {
    const RFrontendContext *context;
    /* R-declared types in definition order: a type after every type it holds by value. */
    uint32_t *aggregates;
    size_t aggregate_count;
    size_t aggregate_capacity;
    /* Types reached only through pointers, placed after the walk that reached them. */
    uint32_t *pending;
    size_t pending_count;
    size_t pending_capacity;
    /* Per aggregate: 1 while its members are visited, 2 once placed. */
    uint8_t *seen;
    /* Interned spellings of the headers of the C-declared types reached. */
    uint32_t *headers;
    size_t header_count;
    size_t header_capacity;
    /* The first aggregate without a C declaration, which R-FFI-0004 keeps out of a signature. */
    uint32_t undeclared;
    bool exhausted;
} RCLibraryApi;

/* One identifier the header declares at file scope, macros included. */
typedef struct RCLibraryName {
    char *bytes;
    size_t length;
} RCLibraryName;

typedef struct RCLibraryNames {
    RFrontendContext *context;
    RCLibraryName *items;
    size_t count;
    size_t capacity;
} RCLibraryNames;

static bool r_c_library_export(const RFrontendContext *context, size_t index) {
    const RSemanticSymbol *symbol = &context->semantic_symbols[index];

    return (symbol->kind == R_SEMANTIC_SYMBOL_FUNCTION) && symbol->is_extern_c &&
           !symbol->is_import && symbol->has_definition && !symbol->poisoned;
}

static void r_c_library_append(RCLibraryApi *api,
                               uint32_t **items,
                               size_t *count,
                               size_t *capacity,
                               uint32_t value) {
    if (!r_grow_array((RFrontendContext *)api->context,
                      (void **)items,
                      capacity,
                      sizeof(**items),
                      *count + 1U)) {
        api->exhausted = true;
        return;
    }
    (*items)[(*count)++] = value;
}

/* R-FFI-0065: a C-declared or opaque type is declared by the headers of its extern block. */
static void r_c_library_add_headers(RCLibraryApi *api, const RSemanticAggregate *aggregate) {
    const RFrontendContext *context = api->context;
    const RExternBlockRecord *record;

    if ((aggregate->extern_block == 0U) ||
        ((size_t)aggregate->extern_block > context->extern_block_count)) {
        return;
    }
    record = &context->extern_blocks[aggregate->extern_block - 1U];
    for (uint32_t header = 0U; header < record->header_count; ++header) {
        const uint32_t name = context->extern_headers[record->first_header + header];
        bool known = false;
        for (size_t other = 0U; other < api->header_count; ++other) {
            known = known || (api->headers[other] == name);
        }
        if (!known) {
            r_c_library_append(api, &api->headers, &api->header_count, &api->header_capacity, name);
        }
    }
}

static void r_c_library_visit(RCLibraryApi *api, RTypeId type_id, bool by_value, uint32_t depth);

static void r_c_library_place(RCLibraryApi *api, uint32_t aggregate_id, uint32_t depth) {
    const RFrontendContext *context = api->context;
    const RSemanticAggregate *aggregate = &context->semantic_aggregates[aggregate_id - 1U];

    if (api->seen[aggregate_id] != 0U) {
        return;
    }
    api->seen[aggregate_id] = 1U;
    if (aggregate->is_opaque || aggregate->is_c_declared) {
        r_c_library_add_headers(api, aggregate);
    } else if (!aggregate->is_repr_c || aggregate->is_tagged || aggregate->poisoned) {
        api->undeclared = (api->undeclared == 0U) ? aggregate_id : api->undeclared;
    } else {
        for (uint32_t field = 0U;
             (aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) && (field < aggregate->field_count);
             ++field) {
            r_c_library_visit(api,
                              context->semantic_fields[aggregate->first_field + field].type,
                              true,
                              depth + 1U);
        }
        r_c_library_append(
            api, &api->aggregates, &api->aggregate_count, &api->aggregate_capacity, aggregate_id);
    }
    api->seen[aggregate_id] = 2U;
}

/* A type held by value is placed before its holder; one behind a pointer needs only its forward
   typedef there, so it is placed after the walk that reached it (pointer cycles). */
static void r_c_library_visit(RCLibraryApi *api, RTypeId type_id, bool by_value, uint32_t depth) {
    const RFrontendContext *context = api->context;
    const RSemanticType *type = r_c_abi_type(context, type_id);

    if ((type == NULL) || (depth > context->options.limits.max_nesting) || api->exhausted) {
        return;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_CONST:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        r_c_library_visit(api, type->base, by_value, depth + 1U);
        return;
    case R_SEMANTIC_TYPE_RAW:
        r_c_library_visit(api, type->base, false, depth + 1U);
        return;
    case R_SEMANTIC_TYPE_RAW_FUNCTION: {
        RTypeId parameter = type->second;
        r_c_library_visit(api, type->base, false, depth + 1U);
        for (uint64_t index = 0U; index < type->length; ++index) {
            const RSemanticType *item = r_c_abi_type(context, parameter);
            if ((item == NULL) || (item->kind != R_SEMANTIC_TYPE_FUNCTION_PARAMETER)) {
                return;
            }
            r_c_library_visit(api, item->base, false, depth + 1U);
            parameter = item->second;
        }
        return;
    }
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM:
        if ((type->base == 0U) || ((size_t)type->base > context->semantic_aggregate_count)) {
            return;
        }
        if (by_value) {
            r_c_library_place(api, (uint32_t)type->base, depth + 1U);
        } else if (api->seen[type->base] == 0U) {
            r_c_library_append(api,
                               &api->pending,
                               &api->pending_count,
                               &api->pending_capacity,
                               (uint32_t)type->base);
        }
        return;
    default:
        return;
    }
}

static bool r_c_library_collect(const RFrontendContext *context, RCLibraryApi *api) {
    (void)memset(api, 0, sizeof(*api));
    api->context = context;
    api->seen =
        r_context_allocate((RFrontendContext *)context, context->semantic_aggregate_count + 1U);
    if (api->seen == NULL) {
        return false;
    }
    (void)memset(api->seen, 0, context->semantic_aggregate_count + 1U);
    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        if (!r_c_library_export(context, index)) {
            continue;
        }
        r_c_library_visit(api, symbol->return_type, true, 0U);
        for (uint32_t parameter = 0U; parameter < symbol->parameter_count; ++parameter) {
            r_c_library_visit(
                api,
                context->semantic_parameter_types[symbol->first_parameter_type + parameter],
                true,
                0U);
        }
    }
    for (size_t index = 0U; (index < api->pending_count) && !api->exhausted; ++index) {
        r_c_library_place(api, api->pending[index], 0U);
    }
    return !api->exhausted;
}

/* Whether a placed type is an R-declared enum, declared by a typedef and enumerator macros. */
static bool r_c_library_enum_type(const RCLibraryApi *api, uint32_t aggregate_id) {
    return api->context->semantic_aggregates[aggregate_id - 1U].kind == R_SEMANTIC_AGGREGATE_ENUM;
}

static void r_c_library_dispose(RCLibraryApi *api) {
    RFrontendContext *context = (RFrontendContext *)api->context;

    r_context_free(context, api->aggregates);
    r_context_free(context, api->pending);
    r_context_free(context, api->seen);
    r_context_free(context, api->headers);
}

/* Keywords of C17 and C++ and the names <stdbool.h>, <stddef.h> and <stdint.h> declare: the
   header is meant for both languages, so no identifier of it may take one. */
static bool r_c_library_keyword(const char *bytes, size_t length) {
    static const char *const keywords[] = {
        "NULL",          "_Alignas",     "_Alignof",       "_Atomic",      "_Bool",
        "_Complex",      "_Generic",     "_Imaginary",     "_Noreturn",    "_Static_assert",
        "_Thread_local", "alignas",      "alignof",        "and",          "and_eq",
        "asm",           "auto",         "bitand",         "bitor",        "bool",
        "break",         "case",         "catch",          "char",         "char16_t",
        "char32_t",      "char8_t",      "class",          "co_await",     "co_return",
        "co_yield",      "compl",        "concept",        "const",        "const_cast",
        "consteval",     "constexpr",    "constinit",      "continue",     "decltype",
        "default",       "delete",       "do",             "double",       "dynamic_cast",
        "else",          "enum",         "explicit",       "export",       "extern",
        "false",         "float",        "for",            "friend",       "goto",
        "if",            "inline",       "int",            "int16_t",      "int32_t",
        "int64_t",       "int8_t",       "intmax_t",       "intptr_t",     "long",
        "max_align_t",   "mutable",      "namespace",      "new",          "noexcept",
        "not",           "not_eq",       "nullptr",        "offsetof",     "operator",
        "or",            "or_eq",        "private",        "protected",    "ptrdiff_t",
        "public",        "register",     "reinterpret_cast", "requires",   "restrict",
        "return",        "short",        "signed",         "size_t",       "sizeof",
        "static",        "static_assert", "static_cast",   "struct",       "switch",
        "template",      "this",         "thread_local",   "throw",        "true",
        "try",           "typedef",      "typeid",         "typename",     "uint16_t",
        "uint32_t",      "uint64_t",     "uint8_t",        "uintmax_t",    "uintptr_t",
        "union",         "unsigned",     "using",          "virtual",      "void",
        "volatile",      "wchar_t",      "while",          "xor",          "xor_eq"};

    for (size_t index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); ++index) {
        if ((strlen(keywords[index]) == length) && (memcmp(keywords[index], bytes, length) == 0)) {
            return true;
        }
    }
    return false;
}

/* A file-scope identifier may also not take a name the C implementation reserves (R-FFI-0057). */
static bool r_c_library_reserved(const char *bytes, size_t length) {
    char text[256];

    if (length >= sizeof(text)) {
        return true;
    }
    (void)memcpy(text, bytes, length);
    text[length] = '\0';
    return r_c_library_keyword(bytes, length) || r_semantic_c_name_reserved(text);
}

static void r_c_library_names_dispose(RCLibraryNames *names) {
    for (size_t index = 0U; index < names->count; ++index) {
        r_context_free(names->context, names->items[index].bytes);
    }
    r_context_free(names->context, names->items);
    names->items = NULL;
    names->count = 0U;
}

/* Adds the identifier first + second + third (third may be NULL). */
static bool r_c_library_name(RCLibraryNames *names,
                             const char *first,
                             size_t first_length,
                             const char *second,
                             const char *third,
                             size_t third_length) {
    const size_t second_length = strlen(second);
    const size_t length = first_length + second_length + third_length;
    char *bytes;

    if (!r_grow_array(names->context,
                      (void **)&names->items,
                      &names->capacity,
                      sizeof(*names->items),
                      names->count + 1U)) {
        return false;
    }
    bytes = r_context_allocate(names->context, length + 1U);
    if (bytes == NULL) {
        return false;
    }
    (void)memcpy(bytes, first, first_length);
    (void)memcpy(bytes + first_length, second, second_length);
    if (third_length != 0U) {
        (void)memcpy(bytes + first_length + second_length, third, third_length);
    }
    bytes[length] = '\0';
    names->items[names->count].bytes = bytes;
    names->items[names->count].length = length;
    names->count += 1U;
    return true;
}

static bool r_c_library_named(const RCLibraryNames *names, const char *bytes, size_t length) {
    for (size_t index = 0U; index < names->count; ++index) {
        if ((names->items[index].length == length) &&
            (memcmp(names->items[index].bytes, bytes, length) == 0)) {
            return true;
        }
    }
    return false;
}

/* The upper-case library name: the prefix of the header's macros. */
static bool r_c_library_macro_prefix(const char *library, char *prefix, size_t capacity) {
    const size_t length = strlen(library);

    if (length + 1U > capacity) {
        return false;
    }
    for (size_t index = 0U; index <= length; ++index) {
        const char c = library[index];
        prefix[index] = ((c >= 'a') && (c <= 'z')) ? (char)(c - 'a' + 'A') : c;
    }
    return true;
}

/* Every file-scope identifier of the header: its macros, the library's own functions, the
   exports, the types and the enumerator macros. */
static bool r_c_library_names(const RFrontendContext *context,
                              const RCLibraryApi *api,
                              const char *library,
                              RCLibraryNames *names) {
    static const char *const macros[] = {
        "_H", "_VERSION", "_STATIC_ASSERT", "_ALIGNOF", "_ALIGNAS"};
    char prefix[256];
    const char *bytes;
    size_t length;

    (void)memset(names, 0, sizeof(*names));
    names->context = (RFrontendContext *)context;
    if (!r_c_library_macro_prefix(library, prefix, sizeof(prefix))) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(macros) / sizeof(macros[0]); ++index) {
        if (!r_c_library_name(names, prefix, strlen(prefix), macros[index], NULL, 0U)) {
            return false;
        }
    }
    if (!r_c_library_name(names, library, strlen(library), "_initialize", NULL, 0U) ||
        !r_c_library_name(names, library, strlen(library), "_shutdown", NULL, 0U)) {
        return false;
    }
    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        if (r_c_library_export(context, index) &&
            (!r_c_abi_intern_text(
                 context, context->semantic_symbols[index].c_name_intern_id, &bytes, &length) ||
             !r_c_library_name(names, bytes, length, "", NULL, 0U))) {
            return false;
        }
    }
    for (size_t index = 0U; index < api->aggregate_count; ++index) {
        const RSemanticAggregate *aggregate =
            &context->semantic_aggregates[api->aggregates[index] - 1U];
        if (!r_c_abi_intern_text(context, aggregate->name_intern_id, &bytes, &length) ||
            !r_c_library_name(names, bytes, length, "", NULL, 0U)) {
            return false;
        }
        for (uint32_t variant = 0U;
             r_c_library_enum_type(api, api->aggregates[index]) && (variant < aggregate->variant_count);
             ++variant) {
            const char *variant_name;
            size_t variant_length;
            if (!r_c_abi_intern_text(
                    context,
                    context->semantic_variants[aggregate->first_variant + variant].name_intern_id,
                    &variant_name,
                    &variant_length) ||
                !r_c_library_name(names, bytes, length, "_", variant_name, variant_length)) {
                return false;
            }
        }
    }
    return true;
}

/* R-FFI-0065: what keeps the API of the library from a C declaration, described in `text`;
   false when nothing does. */
bool r_c_abi_library_problem(const RFrontendContext *context,
                             const char *library,
                             char *text,
                             size_t capacity) {
    RCLibraryApi api;
    RCLibraryNames names;
    const char *bytes;
    size_t length;
    bool problem = false;

    (void)memset(&names, 0, sizeof(names));
    names.context = (RFrontendContext *)context;
    if (!r_c_library_collect(context, &api) ||
        ((api.undeclared == 0U) && !r_c_library_names(context, &api, library, &names))) {
        (void)snprintf(text, capacity, "the API of the library exhausted the allocator");
        problem = true;
    } else if (api.undeclared != 0U) {
        const RSemanticAggregate *aggregate = &context->semantic_aggregates[api.undeclared - 1U];
        if (!r_c_abi_intern_text(context, aggregate->name_intern_id, &bytes, &length)) {
            bytes = "?";
            length = 1U;
        }
        (void)snprintf(text,
                       capacity,
                       "an export passes the type %.*s, which has no C declaration",
                       (int)length,
                       bytes);
        problem = true;
    }
    for (size_t index = 0U; !problem && (index < names.count); ++index) {
        const RCLibraryName *name = &names.items[index];
        bool twice = false;
        for (size_t other = 0U; other < index; ++other) {
            twice = twice || ((names.items[other].length == name->length) &&
                              (memcmp(names.items[other].bytes, name->bytes, name->length) == 0));
        }
        if (twice || r_c_library_reserved(name->bytes, name->length)) {
            (void)snprintf(text,
                           capacity,
                           twice ? "the C header of the library would declare %.*s twice"
                                 : "the C header of the library would declare %.*s, a keyword of "
                                   "C or C++ or a name the C implementation reserves",
                           (int)name->length,
                           name->bytes);
            problem = true;
        }
    }
    for (size_t index = 0U; !problem && (index < api.aggregate_count); ++index) {
        const RSemanticAggregate *aggregate =
            &context->semantic_aggregates[api.aggregates[index] - 1U];
        for (uint32_t field = 0U;
             !problem && (aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) &&
             (field < aggregate->field_count);
             ++field) {
            if (r_c_abi_intern_text(
                    context,
                    context->semantic_fields[aggregate->first_field + field].name_intern_id,
                    &bytes,
                    &length) &&
                r_c_library_keyword(bytes, length)) {
                (void)snprintf(text,
                               capacity,
                               "a member of a type of the library API is named %.*s, a keyword "
                               "of C or C++",
                               (int)length,
                               bytes);
                problem = true;
            }
        }
    }
    r_c_library_names_dispose(&names);
    r_c_library_dispose(&api);
    return problem;
}

/* Whether the parameters of an export keep their R names in the prototype: when no keyword,
   reserved name or file-scope name of the header is among them. Otherwise none is named. */
static bool r_c_library_parameter_names(const RFrontendContext *context,
                                        const RCLibraryNames *names,
                                        RSymbolId function) {
    const char *bytes;
    size_t length;

    for (size_t index = 0U; index < context->semantic_symbol_count; ++index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[index];
        if ((symbol->kind != R_SEMANTIC_SYMBOL_PARAMETER) || (symbol->parent != function)) {
            continue;
        }
        if (!r_c_abi_intern_text(context, symbol->name_intern_id, &bytes, &length) ||
            r_c_library_reserved(bytes, length) || r_c_library_named(names, bytes, length)) {
            return false;
        }
    }
    return true;
}

/* The name of parameter `index` of a function, in declaration order. */
static bool r_c_library_parameter_name(const RFrontendContext *context,
                                       RSymbolId function,
                                       uint32_t index,
                                       char *buffer,
                                       size_t capacity) {
    uint32_t position = 0U;
    const char *bytes;
    size_t length;

    for (size_t symbol_index = 0U; symbol_index < context->semantic_symbol_count; ++symbol_index) {
        const RSemanticSymbol *symbol = &context->semantic_symbols[symbol_index];
        if ((symbol->kind != R_SEMANTIC_SYMBOL_PARAMETER) || (symbol->parent != function) ||
            (position++ != index)) {
            continue;
        }
        if (!r_c_abi_intern_text(context, symbol->name_intern_id, &bytes, &length) ||
            (length + 1U > capacity)) {
            return false;
        }
        (void)memcpy(buffer, bytes, length);
        buffer[length] = '\0';
        return true;
    }
    return false;
}

/* An export's declaration, T NAME(P name, ...); parameter names only with `names`. */
static bool r_c_library_prototype(const RFrontendContext *context,
                                  const RCLibraryNames *names,
                                  RSymbolId function_id,
                                  RCAbiSpell *spell) {
    const RSemanticSymbol *function = &context->semantic_symbols[function_id - 1U];
    const bool named = (names != NULL) && r_c_library_parameter_names(context, names, function_id);
    RCAbiSpell declarator;
    RCAbiSpeller speller = r_c_abi_plain_speller();
    const char *name;
    size_t name_length;
    bool spelled;

    speller.library_names = true;
    r_c_abi_spell_init(&declarator, context);
    spelled = r_c_abi_intern_text(context, function->c_name_intern_id, &name, &name_length) &&
              r_c_abi_spell_append_bytes(&declarator, name, name_length) &&
              r_c_abi_spell_append(&declarator, "(") &&
              ((function->parameter_count != 0U) || r_c_abi_spell_append(&declarator, "void"));
    for (uint32_t index = 0U; spelled && (index < function->parameter_count); ++index) {
        char parameter_name[256];
        parameter_name[0] = '\0';
        spelled =
            ((index == 0U) || r_c_abi_spell_append(&declarator, ", ")) &&
            (!named || r_c_library_parameter_name(
                           context, function_id, index, parameter_name, sizeof(parameter_name))) &&
            r_c_abi_spell_type(
                context,
                context->semantic_parameter_types[function->first_parameter_type + index],
                parameter_name,
                &declarator,
                0U,
                &speller);
    }
    spelled =
        spelled && r_c_abi_spell_append(&declarator, ")") &&
        r_c_abi_spell_type(context, function->return_type, declarator.bytes, spell, 0U, &speller);
    r_c_abi_spell_dispose(&declarator);
    return spelled;
}

static bool
r_c_library_bytes(RFrontendWriteFn writer, void *user_data, const char *bytes, size_t length) {
    return writer(user_data, bytes, length);
}

/* One member of a struct or union definition: alignment, declaration, bit-field width. */
static bool r_c_library_member(const RFrontendContext *context,
                               const char *prefix,
                               const RSemanticField *field,
                               RFrontendWriteFn writer,
                               void *user_data) {
    RCAbiSpell spell;
    RCAbiSpeller speller = r_c_abi_plain_speller();
    const char *name;
    size_t name_length;
    char name_text[256];
    char text[64];
    bool written;

    speller.library_names = true;
    if (!r_c_abi_intern_text(context, field->name_intern_id, &name, &name_length) ||
        (name_length + 1U > sizeof(name_text))) {
        return false;
    }
    (void)memcpy(name_text, name, name_length);
    name_text[name_length] = '\0';
    r_c_abi_spell_init(&spell, context);
    written = r_c_abi_spell_type(context, field->type, name_text, &spell, 0U, &speller) &&
              r_write_text(writer, user_data, "    ");
    if (written && (field->align_attribute != 0U)) {
        (void)snprintf(text, sizeof(text), "_ALIGNAS(%u) ", (unsigned)field->align_attribute);
        written = r_write_text(writer, user_data, prefix) && r_write_text(writer, user_data, text);
    }
    written = written && r_c_library_bytes(writer, user_data, spell.bytes, spell.length);
    if (written && (field->bit_width != 0U)) {
        (void)snprintf(text, sizeof(text), " : %u", (unsigned)field->bit_width);
        written = r_write_text(writer, user_data, text);
    }
    written = written && r_write_text(writer, user_data, ";\n");
    r_c_abi_spell_dispose(&spell);
    return written;
}

/* An R-declared enum: a typedef of its underlying type and one macro per enumerator, since a C
   enum would take the type the C compiler chooses (R-AGG-0005). */
static bool r_c_library_enum(const RFrontendContext *context,
                             const RSemanticAggregate *aggregate,
                             RFrontendWriteFn writer,
                             void *user_data) {
    RCAbiSpell spell;
    RCAbiSpeller speller = r_c_abi_plain_speller();
    const char *name;
    size_t length;
    bool written;

    r_c_abi_spell_init(&spell, context);
    written = r_c_abi_intern_text(context, aggregate->name_intern_id, &name, &length) &&
              r_c_abi_spell_type(
                  context, aggregate->enum_underlying_type, "", &spell, 0U, &speller) &&
              r_write_text(writer, user_data, "typedef ") &&
              r_c_library_bytes(writer, user_data, spell.bytes, spell.length) &&
              r_write_text(writer, user_data, " ") &&
              r_c_library_bytes(writer, user_data, name, length) &&
              r_write_text(writer, user_data, ";\n");
    r_c_abi_spell_dispose(&spell);
    for (uint32_t index = 0U; written && (index < aggregate->variant_count); ++index) {
        const RSemanticVariant *variant =
            &context->semantic_variants[aggregate->first_variant + index];
        const char *variant_name;
        size_t variant_length;
        written =
            r_c_abi_intern_text(context, variant->name_intern_id, &variant_name, &variant_length) &&
            r_write_text(writer, user_data, "#define ") &&
            r_c_library_bytes(writer, user_data, name, length) &&
            r_write_text(writer, user_data, "_") &&
            r_c_library_bytes(writer, user_data, variant_name, variant_length) &&
            r_write_text(writer, user_data, " ((") &&
            r_c_library_bytes(writer, user_data, name, length) &&
            r_write_text(writer, user_data, ")") &&
            r_c_abi_write_variant_value(context, aggregate, variant->value, writer, user_data) &&
            r_write_text(writer, user_data, ")\n");
    }
    return written && r_write_text(writer, user_data, "\n");
}

/* The definition of an R-declared struct or union and the assertions of its layout. */
static bool r_c_library_definition(const RFrontendContext *context,
                                   const char *prefix,
                                   const RSemanticAggregate *aggregate,
                                   RFrontendWriteFn writer,
                                   void *user_data) {
    const char *name;
    size_t length;
    char text[192];
    uint64_t size = 0U;
    uint64_t alignment = 0U;
    bool written;

    if (!r_c_abi_intern_text(context, aggregate->name_intern_id, &name, &length) ||
        !r_semantic_aggregate_layout(context, aggregate, UINT32_MAX, NULL, &size, &alignment)) {
        return false;
    }
    if (aggregate->is_packed && (aggregate->align_attribute != 0U)) {
        (void)snprintf(text,
                       sizeof(text),
                       " __attribute__((packed, aligned(%u)))",
                       (unsigned)aggregate->align_attribute);
    } else if (aggregate->is_packed) {
        (void)snprintf(text, sizeof(text), " __attribute__((packed))");
    } else if (aggregate->align_attribute != 0U) {
        (void)snprintf(
            text, sizeof(text), " __attribute__((aligned(%u)))", (unsigned)aggregate->align_attribute);
    } else {
        text[0] = '\0';
    }
    written = r_write_text(writer, user_data, aggregate->is_union ? "union" : "struct") &&
              r_write_text(writer, user_data, text) && r_write_text(writer, user_data, " ") &&
              r_c_library_bytes(writer, user_data, name, length) &&
              r_write_text(writer, user_data, " {\n");
    for (uint32_t index = 0U; written && (index < aggregate->field_count); ++index) {
        written = r_c_library_member(context,
                                     prefix,
                                     &context->semantic_fields[aggregate->first_field + index],
                                     writer,
                                     user_data);
    }
    (void)snprintf(text,
                   sizeof(text),
                   "_STATIC_ASSERT(sizeof(%.*s) == %" PRIu64 "U, \"size of %.*s\");\n",
                   (int)length,
                   name,
                   size,
                   (int)length,
                   name);
    written = written && r_write_text(writer, user_data, "};\n") &&
              r_write_text(writer, user_data, prefix) && r_write_text(writer, user_data, text);
    (void)snprintf(text,
                   sizeof(text),
                   "_STATIC_ASSERT(%s_ALIGNOF(%.*s) == %" PRIu64 "U, \"alignment of %.*s\");\n",
                   prefix,
                   (int)length,
                   name,
                   alignment,
                   (int)length,
                   name);
    written = written && r_write_text(writer, user_data, prefix) && r_write_text(writer, user_data, text);
    for (uint32_t index = 0U; written && (index < aggregate->field_count); ++index) {
        const RSemanticField *field = &context->semantic_fields[aggregate->first_field + index];
        RSemanticFieldPlace place;
        const char *field_name;
        size_t field_length;
        written = r_semantic_aggregate_layout(
                      context, aggregate, field->layout_index, &place, &size, &alignment) &&
                  r_c_abi_intern_text(context, field->name_intern_id, &field_name, &field_length);
        if (written && (place.bit_width == 0U)) {
            (void)snprintf(text,
                           sizeof(text),
                           "_STATIC_ASSERT(offsetof(%.*s, %.*s) == %" PRIu64
                           "U, \"offset of %.*s.%.*s\");\n",
                           (int)length,
                           name,
                           (int)field_length,
                           field_name,
                           place.offset,
                           (int)length,
                           name,
                           (int)field_length,
                           field_name);
            written = r_write_text(writer, user_data, prefix) && r_write_text(writer, user_data, text);
        }
    }
    return written && r_write_text(writer, user_data, "\n");
}

RFrontendStatus r_c_abi_write_library_header(const RFrontendContext *context,
                                             const char *library,
                                             const char *version,
                                             RFrontendWriteFn writer,
                                             void *user_data) {
    RCLibraryApi api;
    RCLibraryNames names;
    char prefix[256];
    bool written;

    (void)memset(&names, 0, sizeof(names));
    names.context = (RFrontendContext *)context;
    if (!r_c_library_macro_prefix(library, prefix, sizeof(prefix))) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_c_library_collect(context, &api) || !r_c_library_names(context, &api, library, &names)) {
        r_c_library_names_dispose(&names);
        r_c_library_dispose(&api);
        return R_FRONTEND_OUT_OF_MEMORY;
    }
    if (api.undeclared != 0U) {
        r_c_library_names_dispose(&names);
        r_c_library_dispose(&api);
        return R_FRONTEND_NOT_LOWERABLE;
    }
    written = r_write_text(writer, user_data, "/* The C API of the library ") &&
              r_write_text(writer, user_data, library) &&
              r_write_text(writer,
                           user_data,
                           ", written by r-front --emit=c-header (R-FFI-0065). */\n#ifndef ") &&
              r_write_text(writer, user_data, prefix) &&
              r_write_text(writer, user_data, "_H\n#define ") &&
              r_write_text(writer, user_data, prefix) &&
              r_write_text(writer,
                           user_data,
                           "_H\n\n#include <stdbool.h>\n#include <stddef.h>\n#include <stdint.h>\n");
    for (size_t index = 0U; written && (index < api.header_count); ++index) {
        const char *header;
        size_t length;
        written = r_c_abi_intern_text(context, api.headers[index], &header, &length) &&
                  r_write_text(writer, user_data, "#include <") &&
                  r_c_library_bytes(writer, user_data, header, length) &&
                  r_write_text(writer, user_data, ">\n");
    }
    /* The layout assertions in the spelling of each language. */
    written =
        written && r_write_text(writer, user_data, "\n#ifdef __cplusplus\n#define ") &&
        r_write_text(writer, user_data, prefix) &&
        r_write_text(writer,
                     user_data,
                     "_STATIC_ASSERT(condition, message) static_assert(condition, message)\n"
                     "#define ") &&
        r_write_text(writer, user_data, prefix) &&
        r_write_text(writer, user_data, "_ALIGNOF(type) alignof(type)\n#define ") &&
        r_write_text(writer, user_data, prefix) &&
        r_write_text(writer,
                     user_data,
                     "_ALIGNAS(alignment) alignas(alignment)\nextern \"C\" {\n#else\n#define ") &&
        r_write_text(writer, user_data, prefix) &&
        r_write_text(writer,
                     user_data,
                     "_STATIC_ASSERT(condition, message) _Static_assert(condition, message)\n"
                     "#define ") &&
        r_write_text(writer, user_data, prefix) &&
        r_write_text(writer, user_data, "_ALIGNOF(type) _Alignof(type)\n#define ") &&
        r_write_text(writer, user_data, prefix) &&
        r_write_text(writer, user_data, "_ALIGNAS(alignment) _Alignas(alignment)\n#endif\n\n");
    if (written && (version != NULL)) {
        written = r_write_text(writer, user_data, "#define ") &&
                  r_write_text(writer, user_data, prefix) &&
                  r_write_text(writer, user_data, "_VERSION \"") &&
                  r_write_text(writer, user_data, version) &&
                  r_write_text(writer, user_data, "\"\n\n");
    }
    /* R-FFI-0064: the start and the end of the library's runtime. */
    written =
        written &&
        r_write_text(writer,
                     user_data,
                     "/* Starts the runtime of the library: 0 when it started, 1 when the library "
                     "was\n   initialized before, 125, 126 or 127 when the runtime could not start. "
                     "The exports are\n   called after a successful initialization and before "
                     "the shutdown. */\nint32_t ") &&
        r_write_text(writer, user_data, library) &&
        r_write_text(writer,
                     user_data,
                     "_initialize(void);\n/* Ends the runtime of an initialized library once its "
                     "tasks ended. */\nvoid ") &&
        r_write_text(writer, user_data, library) &&
        r_write_text(writer, user_data, "_shutdown(void);\n\n");
    for (size_t index = 0U; written && (index < api.aggregate_count); ++index) {
        const RSemanticAggregate *aggregate =
            &context->semantic_aggregates[api.aggregates[index] - 1U];
        if (r_c_library_enum_type(&api, api.aggregates[index])) {
            written = r_c_library_enum(context, aggregate, writer, user_data);
        }
    }
    {
        bool forward = false;
        for (size_t index = 0U; written && (index < api.aggregate_count); ++index) {
            const RSemanticAggregate *aggregate =
                &context->semantic_aggregates[api.aggregates[index] - 1U];
            const char *name;
            size_t length;
            if (r_c_library_enum_type(&api, api.aggregates[index])) {
                continue;
            }
            forward = true;
            written =
                r_c_abi_intern_text(context, aggregate->name_intern_id, &name, &length) &&
                r_write_text(
                    writer, user_data, aggregate->is_union ? "typedef union " : "typedef struct ") &&
                r_c_library_bytes(writer, user_data, name, length) &&
                r_write_text(writer, user_data, " ") &&
                r_c_library_bytes(writer, user_data, name, length) &&
                r_write_text(writer, user_data, ";\n");
        }
        written = written && (!forward || r_write_text(writer, user_data, "\n"));
    }
    for (size_t index = 0U; written && (index < api.aggregate_count); ++index) {
        const RSemanticAggregate *aggregate =
            &context->semantic_aggregates[api.aggregates[index] - 1U];
        if (!r_c_library_enum_type(&api, api.aggregates[index])) {
            written = r_c_library_definition(context, prefix, aggregate, writer, user_data);
        }
    }
    for (size_t index = 0U; written && (index < context->semantic_symbol_count); ++index) {
        RCAbiSpell spell;
        if (!r_c_library_export(context, index)) {
            continue;
        }
        r_c_abi_spell_init(&spell, context);
        written = r_c_library_prototype(context, &names, (RSymbolId)(index + 1U), &spell) &&
                  r_c_library_bytes(writer, user_data, spell.bytes, spell.length) &&
                  r_write_text(writer, user_data, ";\n");
        r_c_abi_spell_dispose(&spell);
    }
    written = written && r_write_text(writer, user_data, "\n#ifdef __cplusplus\n}\n#endif\n\n#endif\n");
    r_c_library_names_dispose(&names);
    r_c_library_dispose(&api);
    return written ? R_FRONTEND_OK : r_c_abi_resource_status(context, R_FRONTEND_NOT_LOWERABLE);
}

/* An enumerator as the decimal value of its underlying type. */
static bool r_c_library_enumerator_text(const RFrontendContext *context,
                                        const RSemanticAggregate *aggregate,
                                        uint64_t bits,
                                        char *text,
                                        size_t capacity) {
    bool is_signed = false;
    uint32_t width = 0U;
    uint64_t mask;

    if (!r_semantic_integer_properties_public(
            r_semantic_integer_representation(context, aggregate->enum_underlying_type),
            &is_signed,
            &width) ||
        (width == 0U) || (width > 64U)) {
        return false;
    }
    mask = (width == 64U) ? UINT64_MAX : (UINT64_C(1) << width) - 1U;
    if (is_signed && ((bits & (UINT64_C(1) << (width - 1U))) != 0U)) {
        (void)snprintf(text, capacity, "-%" PRIu64, (UINT64_C(0) - bits) & mask);
    } else {
        (void)snprintf(text, capacity, "%" PRIu64, bits & mask);
    }
    return true;
}

static bool
r_c_library_json(RFrontendWriteFn writer, void *user_data, const char *bytes, size_t length) {
    return r_abi_write_json_string(writer, user_data, bytes, length);
}

/* The members of an R-declared struct or union in the baseline. */
static bool r_c_library_baseline_members(const RFrontendContext *context,
                                         const RSemanticAggregate *aggregate,
                                         RFrontendWriteFn writer,
                                         void *user_data) {
    bool written = r_write_text(writer, user_data, "\"members\": [");

    for (uint32_t index = 0U; written && (index < aggregate->field_count); ++index) {
        const RSemanticField *field = &context->semantic_fields[aggregate->first_field + index];
        RSemanticFieldPlace place;
        RCAbiSpell spell;
        RCAbiSpeller speller = r_c_abi_plain_speller();
        const char *name;
        size_t length;
        uint64_t size = 0U;
        uint64_t alignment = 0U;
        char text[192];

        (void)memset(&place, 0, sizeof(place));
        speller.library_names = true;
        r_c_abi_spell_init(&spell, context);
        written = r_semantic_aggregate_layout(
                      context, aggregate, field->layout_index, &place, &size, &alignment) &&
                  r_c_abi_spell_type(context, field->type, "", &spell, 0U, &speller) &&
                  r_c_abi_intern_text(context, field->name_intern_id, &name, &length);
        (void)snprintf(text,
                       sizeof(text),
                       ", \"offset\": %" PRIu64 ", \"bit_offset\": %u, \"bit_width\": %u, "
                       "\"alignment\": %" PRIu64 "}",
                       place.offset,
                       (unsigned)place.bit_offset,
                       (unsigned)place.bit_width,
                       place.alignment);
        written = written &&
                  r_write_text(writer,
                               user_data,
                               index == 0U ? "\n        {\"name\": " : ",\n        {\"name\": ") &&
                  r_c_library_json(writer, user_data, name, length) &&
                  r_write_text(writer, user_data, ", \"type\": ") &&
                  r_c_library_json(writer, user_data, spell.bytes, spell.length) &&
                  r_write_text(writer, user_data, text);
        r_c_abi_spell_dispose(&spell);
    }
    return written &&
           r_write_text(writer, user_data, aggregate->field_count == 0U ? "]" : "\n      ]");
}

/* The enumerators of an R-declared enum in the baseline. */
static bool r_c_library_baseline_values(const RFrontendContext *context,
                                        const RSemanticAggregate *aggregate,
                                        RFrontendWriteFn writer,
                                        void *user_data) {
    bool written = r_write_text(writer, user_data, "\"values\": [");

    for (uint32_t index = 0U; written && (index < aggregate->variant_count); ++index) {
        const RSemanticVariant *variant =
            &context->semantic_variants[aggregate->first_variant + index];
        const char *name;
        size_t length;
        char value[48];
        written = r_c_abi_intern_text(context, variant->name_intern_id, &name, &length) &&
                  r_c_library_enumerator_text(
                      context, aggregate, variant->value, value, sizeof(value)) &&
                  r_write_text(writer,
                               user_data,
                               index == 0U ? "\n        {\"name\": " : ",\n        {\"name\": ") &&
                  r_c_library_json(writer, user_data, name, length) &&
                  r_write_text(writer, user_data, ", \"value\": ") &&
                  r_c_library_json(writer, user_data, value, strlen(value)) &&
                  r_write_text(writer, user_data, "}");
    }
    return written &&
           r_write_text(writer, user_data, aggregate->variant_count == 0U ? "]" : "\n      ]");
}

RFrontendStatus r_c_abi_write_library_baseline(const RFrontendContext *context,
                                               const char *library,
                                               const char *version,
                                               const char *compatibility_version,
                                               RFrontendWriteFn writer,
                                               void *user_data) {
    const char *target = r_semantic_target_triple();
    RCLibraryApi api;
    bool first = true;
    bool written;

    if (!r_c_library_collect(context, &api)) {
        r_c_library_dispose(&api);
        return R_FRONTEND_OUT_OF_MEMORY;
    }
    if (api.undeclared != 0U) {
        r_c_library_dispose(&api);
        return R_FRONTEND_NOT_LOWERABLE;
    }
    written =
        r_write_text(
            writer, user_data, "{\n  \"schema\": \"r-abi-baseline-0.1\",\n  \"library\": ") &&
        r_c_library_json(writer, user_data, library, strlen(library)) &&
        r_write_text(writer, user_data, ",\n  \"target\": ") &&
        r_c_library_json(writer, user_data, target, strlen(target)) &&
        r_write_text(writer, user_data, ",\n  \"version\": ") &&
        r_c_library_json(writer, user_data, version, strlen(version)) &&
        r_write_text(writer, user_data, ",\n  \"compatibility_version\": ") &&
        r_c_library_json(writer, user_data, compatibility_version, strlen(compatibility_version)) &&
        r_write_text(writer, user_data, ",\n  \"exports\": [");
    for (size_t index = 0U; written && (index < context->semantic_symbol_count); ++index) {
        RCAbiSpell spell;
        const char *name;
        size_t length;
        if (!r_c_library_export(context, index)) {
            continue;
        }
        r_c_abi_spell_init(&spell, context);
        written =
            r_c_library_prototype(context, NULL, (RSymbolId)(index + 1U), &spell) &&
            r_c_abi_intern_text(
                context, context->semantic_symbols[index].c_name_intern_id, &name, &length) &&
            r_write_text(writer, user_data, first ? "\n    {\"name\": " : ",\n    {\"name\": ") &&
            r_c_library_json(writer, user_data, name, length) &&
            r_write_text(writer, user_data, ", \"prototype\": ") &&
            r_c_library_json(writer, user_data, spell.bytes, spell.length) &&
            r_write_text(writer, user_data, "}");
        r_c_abi_spell_dispose(&spell);
        first = false;
    }
    written = written && r_write_text(writer,
                                      user_data,
                                      first ? "],\n  \"types\": [" : "\n  ],\n  \"types\": [");
    for (size_t index = 0U; written && (index < api.aggregate_count); ++index) {
        const RSemanticAggregate *aggregate =
            &context->semantic_aggregates[api.aggregates[index] - 1U];
        const bool is_enum = r_c_library_enum_type(&api, api.aggregates[index]);
        const char *name;
        size_t length;
        uint64_t size = 0U;
        uint64_t alignment = 0U;
        char text[160];
        written =
            is_enum
                ? r_semantic_type_layout(
                      context, aggregate->enum_underlying_type, &size, &alignment)
                : r_semantic_aggregate_layout(context, aggregate, UINT32_MAX, NULL, &size, &alignment);
        (void)snprintf(text,
                       sizeof(text),
                       "\",\n      \"size\": %" PRIu64 ", \"alignment\": %" PRIu64
                       ", \"packed\": %s,\n      ",
                       size,
                       alignment,
                       aggregate->is_packed ? "true" : "false");
        written =
            written && r_c_abi_intern_text(context, aggregate->name_intern_id, &name, &length) &&
            r_write_text(writer, user_data, index == 0U ? "\n    {\"name\": " : ",\n    {\"name\": ") &&
            r_c_library_json(writer, user_data, name, length) &&
            r_write_text(writer, user_data, ", \"kind\": \"") &&
            r_write_text(
                writer, user_data, is_enum ? "enum" : (aggregate->is_union ? "union" : "struct")) &&
            r_write_text(writer, user_data, text) &&
            (is_enum ? r_c_library_baseline_values(context, aggregate, writer, user_data)
                     : r_c_library_baseline_members(context, aggregate, writer, user_data)) &&
            r_write_text(writer, user_data, "}");
    }
    written = written &&
              r_write_text(writer, user_data, api.aggregate_count == 0U ? "]\n}\n" : "\n  ]\n}\n");
    r_c_library_dispose(&api);
    return written ? R_FRONTEND_OK : r_c_abi_resource_status(context, R_FRONTEND_NOT_LOWERABLE);
}
