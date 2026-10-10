#include "clang_internal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*
 * C headers through libclang (B9.1). The verifier translation unit of R-FFI-0042 is the one
 * --emit=abi-verifier writes; instead of a C compiler of the build, r-front parses it in process
 * with the libclang of the pinned toolchain, for the target manifest's LLVM triple, in the system
 * root of the pinned SDK, with the C17 options and warnings as errors of the generated-C contract.
 * A probe that does not compile is a declaration that disagrees with its header: the writer marks
 * the R declaration before its probes, so the R-DIAG-FFI-004 of a failed probe lands on it, and
 * libclang's own text is kept as the header report the CLI prints after the diagnostics.
 */

/* The options of the generated-C contract (R-CMAP-0024) that the verifier is compiled with. */
static const char *const r_clang_contract_options[] = {
    "-x",
    "c",
    "-std=c17",
    "-pedantic-errors",
    "-Wall",
    "-Wextra",
    "-Werror",
    "-Wconversion",
    "-Wsign-conversion",
    "-Wshadow",
    "-Wstrict-prototypes",
    "-Wmissing-prototypes",
};

#ifndef R_CLANG_RESOURCE_DIR
#error "R_CLANG_RESOURCE_DIR names the resource directory of the linked libclang"
#endif

#define R_CLANG_CONTRACT_OPTION_COUNT                                                              \
    (sizeof(r_clang_contract_options) / sizeof(r_clang_contract_options[0]))
#define R_CLANG_SDK_SETTINGS "/SDKSettings.json"

/* One mark: the byte offset in the unit where the probes of an R declaration begin. */
typedef struct RClangMark {
    size_t offset;
    RSourceSpan span;
    const char *rule_id;
    bool reported;
} RClangMark;

typedef struct RClangUnit {
    RFrontendContext *context;
    char *bytes;
    size_t length;
    size_t capacity;
    RClangMark *marks;
    size_t mark_count;
    size_t mark_capacity;
    bool failed;
} RClangUnit;

static bool r_clang_unit_write(void *user_data, const char *bytes, size_t length) {
    RClangUnit *unit = user_data;

    /* One byte more for the terminator libclang does not need but the report search uses. */
    if ((length >= SIZE_MAX - unit->length) || !r_grow_array(unit->context,
                                                             (void **)&unit->bytes,
                                                             &unit->capacity,
                                                             sizeof(char),
                                                             unit->length + length + 1U)) {
        unit->failed = true;
        return false;
    }
    if (length != 0U) {
        (void)memcpy(unit->bytes + unit->length, bytes, length);
    }
    unit->length += length;
    unit->bytes[unit->length] = '\0';
    return true;
}

static void r_clang_unit_mark(void *user_data, RSourceSpan span, const char *rule_id) {
    RClangUnit *unit = user_data;

    if (!r_grow_array(unit->context,
                      (void **)&unit->marks,
                      &unit->mark_capacity,
                      sizeof(RClangMark),
                      unit->mark_count + 1U)) {
        unit->failed = true;
        return;
    }
    unit->marks[unit->mark_count].offset = unit->length;
    unit->marks[unit->mark_count].span = span;
    unit->marks[unit->mark_count].rule_id = rule_id;
    unit->marks[unit->mark_count].reported = false;
    unit->mark_count += 1U;
}

/* The mark whose probes contain offset; a diagnostic before the first probe (a header that does
   not compile or is not found) or inside a header belongs to the first mark. */
static RClangMark *r_clang_unit_mark_at(RClangUnit *unit, size_t offset, bool in_unit) {
    size_t index;
    RClangMark *found;

    if (unit->mark_count == 0U) {
        return NULL;
    }
    found = &unit->marks[0];
    if (!in_unit) {
        return found;
    }
    for (index = 0U; index < unit->mark_count; ++index) {
        if (unit->marks[index].offset > offset) {
            break;
        }
        found = &unit->marks[index];
    }
    return found;
}

bool r_clang_report_append(RFrontendContext *context, const char *text, size_t length) {
    if ((length >= SIZE_MAX - context->header_report_length) ||
        !r_grow_array(context,
                      (void **)&context->header_report,
                      &context->header_report_capacity,
                      sizeof(char),
                      context->header_report_length + length + 1U)) {
        return false;
    }
    if (length != 0U) {
        (void)memcpy(context->header_report + context->header_report_length, text, length);
    }
    context->header_report_length += length;
    context->header_report[context->header_report_length] = '\0';
    return true;
}

/* "Homebrew clang version 22.1.8 (...)" names the release after "clang version ". */
bool r_frontend_header_version(char *buffer, size_t capacity) {
    static const char marker[] = "clang version ";
    CXString version;
    const char *text;
    const char *start;
    size_t length = 0U;
    bool found = false;

    if ((buffer == NULL) || (capacity == 0U)) {
        return false;
    }
    version = clang_getClangVersion();
    text = clang_getCString(version);
    start = text != NULL ? strstr(text, marker) : NULL;
    if (start != NULL) {
        start += sizeof(marker) - 1U;
        while (((start[length] >= '0') && (start[length] <= '9')) || (start[length] == '.')) {
            length += 1U;
        }
        if ((length != 0U) && (length < capacity)) {
            (void)memcpy(buffer, start, length);
            buffer[length] = '\0';
            found = true;
        }
    }
    clang_disposeString(version);
    return found;
}

/* The "Version" of the SDK's SDKSettings.json equals the version of toolchain.sdk. */
static bool r_clang_sdk_matches(RFrontendContext *context, const char *sysroot) {
    static const char key[] = "\"Version\"";
    enum {
        R_CLANG_SETTINGS_LIMIT = 65536
    };
    char path[4096];
    char *settings;
    const char *cursor;
    const char *expected = r_frontend_target_sdk_version();
    size_t expected_length = strlen(expected);
    size_t length;
    bool matches = false;
    FILE *stream;
    int written = snprintf(path, sizeof(path), "%s%s", sysroot, R_CLANG_SDK_SETTINGS);

    if ((written < 0) || ((size_t)written >= sizeof(path))) {
        return false;
    }
    settings = r_context_allocate(context, R_CLANG_SETTINGS_LIMIT);
    if (settings == NULL) {
        return false;
    }
    stream = fopen(path, "rb");
    if (stream != NULL) {
        length = fread(settings, 1U, R_CLANG_SETTINGS_LIMIT - 1U, stream);
        (void)fclose(stream);
        settings[length] = '\0';
        cursor = strstr(settings, key);
        if (cursor != NULL) {
            cursor += sizeof(key) - 1U;
            while ((*cursor == ' ') || (*cursor == ':') || (*cursor == '\t') || (*cursor == '\n')) {
                cursor += 1U;
            }
            matches = (*cursor == '"') && (strncmp(cursor + 1U, expected, expected_length) == 0) &&
                      (cursor[1U + expected_length] == '"');
        }
    }
    r_context_free(context, settings);
    return matches;
}

/* Reports the error diagnostics of the unit: R-DIAG-FFI-004 once per marked R declaration. */
static bool r_clang_report(RClangUnit *unit, CXTranslationUnit translation, bool *failed) {
    RFrontendContext *context = unit->context;
    const unsigned count = clang_getNumDiagnostics(translation);
    unsigned index;

    for (index = 0U; index < count; ++index) {
        CXDiagnostic diagnostic = clang_getDiagnostic(translation, index);
        const enum CXDiagnosticSeverity severity = clang_getDiagnosticSeverity(diagnostic);
        CXSourceLocation location;
        CXFile file = NULL;
        unsigned offset = 0U;
        bool in_unit = false;
        CXString text;
        const char *bytes;
        RClangMark *mark;
        bool appended;

        if (severity < CXDiagnostic_Error) {
            clang_disposeDiagnostic(diagnostic);
            continue;
        }
        *failed = true;
        location = clang_getDiagnosticLocation(diagnostic);
        clang_getSpellingLocation(location, &file, NULL, NULL, &offset);
        if (file != NULL) {
            CXString name = clang_getFileName(file);
            const char *spelled = clang_getCString(name);

            in_unit = (spelled != NULL) && (strcmp(spelled, R_CLANG_UNIT_NAME) == 0);
            clang_disposeString(name);
        }
        text = clang_formatDiagnostic(diagnostic,
                                      CXDiagnostic_DisplaySourceLocation |
                                          CXDiagnostic_DisplayColumn | CXDiagnostic_DisplayOption);
        bytes = clang_getCString(text);
        appended = r_clang_report_append(
                       context, bytes != NULL ? bytes : "", bytes != NULL ? strlen(bytes) : 0U) &&
                   r_clang_report_append(context, "\n", 1U);
        clang_disposeString(text);
        clang_disposeDiagnostic(diagnostic);
        if (!appended) {
            return false;
        }
        mark = r_clang_unit_mark_at(unit, (size_t)offset, in_unit);
        if ((mark != NULL) && !mark->reported) {
            mark->reported = true;
            if (!r_add_diagnostic_phase(context,
                                        R_DIAGNOSTIC_PHASE_SEMANTIC,
                                        "R-DIAG-FFI-004",
                                        in_unit ? mark->rule_id : "R-FFI-0042",
                                        "declaration disagrees with its verified C header",
                                        R_DIAGNOSTIC_ERROR,
                                        mark->span)) {
                return false;
            }
        }
    }
    return true;
}

bool r_clang_check_toolchain(RFrontendContext *context, const RFrontendHeaderOptions *options) {
    char version[32];

    return r_frontend_header_version(version, sizeof(version)) &&
           (strcmp(version, r_frontend_target_c_compiler_version()) == 0) &&
           r_clang_sdk_matches(context, options->sysroot);
}

const char **
r_clang_arguments(RFrontendContext *context, const RFrontendHeaderOptions *options, size_t *count) {
    const char **arguments;
    size_t argument_count = 0U;
    size_t index;

    if (options->root_count >
        (SIZE_MAX / (2U * sizeof(char *))) - R_CLANG_CONTRACT_OPTION_COUNT - 8U) {
        return NULL;
    }
    arguments = r_context_allocate(
        context, (R_CLANG_CONTRACT_OPTION_COUNT + 6U + 2U * options->root_count) * sizeof(char *));
    if (arguments == NULL) {
        return NULL;
    }
    for (index = 0U; index < R_CLANG_CONTRACT_OPTION_COUNT; ++index) {
        arguments[argument_count++] = r_clang_contract_options[index];
    }
    arguments[argument_count++] = "-target";
    arguments[argument_count++] = r_frontend_backend_triple();
    arguments[argument_count++] = "-isysroot";
    arguments[argument_count++] = options->sysroot;
    arguments[argument_count++] = "-resource-dir";
    arguments[argument_count++] = R_CLANG_RESOURCE_DIR;
    for (index = 0U; index < options->root_count; ++index) {
        arguments[argument_count++] = "-I";
        arguments[argument_count++] = options->roots[index];
    }
    *count = argument_count;
    return arguments;
}

RFrontendStatus r_clang_parse(RFrontendContext *context,
                              const RFrontendHeaderOptions *options,
                              const char *source,
                              size_t length,
                              unsigned flags,
                              CXIndex *index,
                              CXTranslationUnit *unit) {
    size_t argument_count = 0U;
    const char **arguments = r_clang_arguments(context, options, &argument_count);
    struct CXUnsavedFile unsaved;
    RFrontendStatus status = R_FRONTEND_OK;

    *index = NULL;
    *unit = NULL;
    if (arguments == NULL) {
        return R_FRONTEND_OUT_OF_MEMORY;
    }
    unsaved.Filename = R_CLANG_UNIT_NAME;
    unsaved.Contents = source != NULL ? source : "";
    unsaved.Length = (unsigned long)length;
    *index = clang_createIndex(0, 0);
    if ((*index == NULL) || (clang_parseTranslationUnit2(*index,
                                                         R_CLANG_UNIT_NAME,
                                                         arguments,
                                                         (int)argument_count,
                                                         &unsaved,
                                                         1U,
                                                         flags,
                                                         unit) != CXError_Success)) {
        status = R_FRONTEND_INTERNAL_ERROR;
    }
    r_context_free(context, arguments);
    return status;
}

bool r_clang_report_errors(RFrontendContext *context, CXTranslationUnit unit, bool *failed) {
    const unsigned count = clang_getNumDiagnostics(unit);
    unsigned index;

    for (index = 0U; index < count; ++index) {
        CXDiagnostic diagnostic = clang_getDiagnostic(unit, index);
        bool appended = true;

        if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error) {
            CXString text =
                clang_formatDiagnostic(diagnostic,
                                       CXDiagnostic_DisplaySourceLocation |
                                           CXDiagnostic_DisplayColumn | CXDiagnostic_DisplayOption);
            const char *bytes = clang_getCString(text);

            *failed = true;
            appended = r_clang_report_append(context,
                                             bytes != NULL ? bytes : "",
                                             bytes != NULL ? strlen(bytes) : 0U) &&
                       r_clang_report_append(context, "\n", 1U);
            clang_disposeString(text);
        }
        clang_disposeDiagnostic(diagnostic);
        if (!appended) {
            return false;
        }
    }
    return true;
}

typedef struct RClangLinkNames {
    RFrontendContext *context;
    const char *prefix;
    bool failed;
    bool exhausted;
} RClangLinkNames;

/* R-CMAP-0026: an import binds to its C name as the target spells a global symbol; a header that
   binds the declaration to another symbol (an asm label such as realpath$DARWIN_EXTSN) requires
   a shim with the default C ABI, which the program supplies itself. */
static enum CXChildVisitResult
r_clang_link_name_visit(CXCursor cursor, CXCursor parent, CXClientData data) {
    RClangLinkNames *names = data;
    RFrontendContext *context = names->context;
    const enum CXCursorKind kind = clang_getCursorKind(cursor);
    CXString spelling;
    CXString mangling;
    const char *name;
    const char *linked;
    size_t index;

    (void)parent;
    if ((kind != CXCursor_FunctionDecl) && (kind != CXCursor_VarDecl)) {
        return CXChildVisit_Continue;
    }
    spelling = clang_getCursorSpelling(cursor);
    mangling = clang_Cursor_getMangling(cursor);
    name = clang_getCString(spelling);
    linked = clang_getCString(mangling);
    for (index = 0U; (name != NULL) && (linked != NULL) && (index < context->semantic_symbol_count);
         ++index) {
        RSemanticSymbol *symbol = &context->semantic_symbols[index];
        const RInternEntry *entry;
        const size_t prefix_length = strlen(names->prefix);

        if (!symbol->is_import || symbol->poisoned || symbol->decorated_header_symbol ||
            ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) &&
             (symbol->kind != R_SEMANTIC_SYMBOL_MODULE_OBJECT)) ||
            (symbol->c_name_intern_id == UINT32_C(0)) ||
            ((size_t)symbol->c_name_intern_id > context->intern_count)) {
            continue;
        }
        entry = &context->intern_entries[(size_t)symbol->c_name_intern_id - 1U];
        if ((strlen(name) != entry->length) || (memcmp(name, entry->bytes, entry->length) != 0)) {
            continue;
        }
        if ((strncmp(linked, names->prefix, prefix_length) == 0) &&
            (strlen(linked + prefix_length) == entry->length) &&
            (memcmp(linked + prefix_length, entry->bytes, entry->length) == 0)) {
            continue;
        }
        symbol->decorated_header_symbol = true;
        names->failed = true;
        if (!r_clang_report_append(context, name, strlen(name)) ||
            !r_clang_report_append(context,
                                   ": the header binds it to the symbol ",
                                   sizeof(": the header binds it to the symbol ") - 1U) ||
            !r_clang_report_append(context, linked, strlen(linked)) ||
            !r_clang_report_append(context, "\n", 1U) ||
            !r_add_diagnostic_phase(context,
                                    R_DIAGNOSTIC_PHASE_SEMANTIC,
                                    "R-DIAG-FFI-004",
                                    "R-CMAP-0026",
                                    "the verified header binds the import to a decorated symbol",
                                    R_DIAGNOSTIC_ERROR,
                                    symbol->name_span)) {
            names->exhausted = true;
            break;
        }
    }
    clang_disposeString(mangling);
    clang_disposeString(spelling);
    return names->exhausted ? CXChildVisit_Break : CXChildVisit_Continue;
}

RFrontendStatus r_frontend_verify_headers(RFrontendContext *context,
                                          const RFrontendHeaderOptions *options) {
    RClangUnit unit;
    RCAbiVerifierMarks marks;
    CXIndex clang_index = NULL;
    CXTranslationUnit translation = NULL;
    RFrontendStatus status;
    bool failed = false;

    if ((context == NULL) || (options == NULL) || (options->sysroot == NULL) ||
        ((options->root_count != 0U) && (options->roots == NULL))) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_clang_check_toolchain(context, options)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    context->header_report_length = 0U;
    (void)memset(&unit, 0, sizeof(unit));
    unit.context = context;
    marks.mark = r_clang_unit_mark;
    marks.user_data = &unit;
    status = r_c_abi_write_verifier(context, r_clang_unit_write, &unit, &marks);
    if ((status == R_FRONTEND_OK) && unit.failed) {
        status = R_FRONTEND_OUT_OF_MEMORY;
    }
    if (status == R_FRONTEND_OK) {
        status = r_clang_parse(context,
                               options,
                               unit.bytes,
                               unit.length,
                               CXTranslationUnit_None,
                               &clang_index,
                               &translation);
    }
    if (status == R_FRONTEND_OK) {
        status = !r_clang_report(&unit, translation, &failed) ? R_FRONTEND_OUT_OF_MEMORY
                 : failed                                     ? R_FRONTEND_INVALID_SOURCE
                                                              : R_FRONTEND_OK;
    }
    if (status == R_FRONTEND_OK) {
        RClangLinkNames names;

        names.context = context;
        names.prefix = r_frontend_target_global_prefix();
        names.failed = false;
        names.exhausted = false;
        (void)clang_visitChildren(
            clang_getTranslationUnitCursor(translation), r_clang_link_name_visit, &names);
        status = names.exhausted ? R_FRONTEND_OUT_OF_MEMORY
                 : names.failed  ? R_FRONTEND_INVALID_SOURCE
                                 : R_FRONTEND_OK;
    }
    if (translation != NULL) {
        clang_disposeTranslationUnit(translation);
    }
    if (clang_index != NULL) {
        clang_disposeIndex(clang_index);
    }
    r_context_free(context, unit.marks);
    r_context_free(context, unit.bytes);
    return status;
}

/* B10.2 (R-FFI-0065): the header of a library is parsed for the target with the options of the
   generated-C contract before it is written, so its layout assertions hold for the target's C
   compiler and its includes resolve against the header roots. */
RFrontendStatus r_frontend_emit_c_header(RFrontendContext *context,
                                         const RFrontendArtifactOptions *artifact_options,
                                         const RFrontendHeaderOptions *options,
                                         const char *version,
                                         RFrontendWriteFn writer,
                                         void *user_data) {
    RClangUnit unit;
    CXIndex clang_index = NULL;
    CXTranslationUnit translation = NULL;
    RFrontendStatus status;
    bool failed = false;

    if ((context == NULL) || (artifact_options == NULL) ||
        (artifact_options->library_name == NULL) || (options == NULL) ||
        (options->sysroot == NULL) || ((options->root_count != 0U) && (options->roots == NULL)) ||
        (writer == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!r_clang_check_toolchain(context, options)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    context->header_report_length = 0U;
    (void)memset(&unit, 0, sizeof(unit));
    unit.context = context;
    status = r_c_abi_write_library_header(
        context, artifact_options->library_name, version, r_clang_unit_write, &unit);
    if ((status == R_FRONTEND_OK) && unit.failed) {
        status = R_FRONTEND_OUT_OF_MEMORY;
    }
    if (status == R_FRONTEND_OK) {
        status = r_clang_parse(context,
                               options,
                               unit.bytes,
                               unit.length,
                               CXTranslationUnit_None,
                               &clang_index,
                               &translation);
    }
    if (status == R_FRONTEND_OK) {
        status = !r_clang_report_errors(context, translation, &failed) ? R_FRONTEND_OUT_OF_MEMORY
                 : failed                                              ? R_FRONTEND_INVALID_SOURCE
                                                                       : R_FRONTEND_OK;
    }
    if ((status == R_FRONTEND_OK) && !writer(user_data, unit.bytes, unit.length)) {
        status = R_FRONTEND_IO_ERROR;
    }
    if (translation != NULL) {
        clang_disposeTranslationUnit(translation);
    }
    if (clang_index != NULL) {
        clang_disposeIndex(clang_index);
    }
    r_context_free(context, unit.marks);
    r_context_free(context, unit.bytes);
    return status;
}

const char *r_frontend_header_report(const RFrontendContext *context, size_t *length) {
    if ((context == NULL) || (context->header_report_length == 0U)) {
        if (length != NULL) {
            *length = 0U;
        }
        return NULL;
    }
    if (length != NULL) {
        *length = context->header_report_length;
    }
    return context->header_report;
}
