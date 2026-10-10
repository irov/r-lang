#include "allocation.h"
#include "r_frontend.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum REmitKind {
    R_EMIT_TOKENS = 0,
    R_EMIT_CST,
    R_EMIT_AST,
    R_EMIT_HIR,
    R_EMIT_MIR,
    R_EMIT_INTERFACE,
    R_EMIT_LINK_PLAN,
    R_EMIT_BUNDLE,
    R_EMIT_ABI_VERIFIER,
    R_EMIT_ABI_INVENTORY,
    R_EMIT_ABI_RECORD,
    R_EMIT_BINDINGS,
    R_EMIT_LLVM_IR,
    R_EMIT_OBJECT,
    /* B10.1: the exported symbols of a library and the library linked as a shared library. */
    R_EMIT_EXPORT_LIST,
    R_EMIT_SHARED_LIBRARY,
    /* B10.2: the C header of a library and its ABI baseline. */
    R_EMIT_C_HEADER,
    R_EMIT_ABI_BASELINE
} REmitKind;

/* The artifacts that are a whole program: LLVM IR or an object from the LLVM emitter, and the
   artifacts of a library built from them. */
static bool r_cli_emits_program(REmitKind emit) {
    return (emit == R_EMIT_LLVM_IR) || (emit == R_EMIT_OBJECT) || (emit == R_EMIT_EXPORT_LIST) ||
           (emit == R_EMIT_SHARED_LIBRARY) || (emit == R_EMIT_C_HEADER) ||
           (emit == R_EMIT_ABI_BASELINE);
}

/* The artifacts of a library. */
static bool r_cli_emits_library(REmitKind emit) {
    return (emit == R_EMIT_EXPORT_LIST) || (emit == R_EMIT_SHARED_LIBRARY) ||
           (emit == R_EMIT_C_HEADER) || (emit == R_EMIT_ABI_BASELINE);
}

typedef enum RDiagnosticsKind {
    R_DIAGNOSTICS_TEXT = 0,
    R_DIAGNOSTICS_JSON
} RDiagnosticsKind;

typedef struct RModuleMapEntry {
    char *module_name;
    char *path;
    /* R-MOD-0002: the minimum profile of a library-map entry; NULL for a module-map entry. */
    char *profile;
    bool loaded;
} RModuleMapEntry;

typedef struct RCliOptions {
    REmitKind emit;
    RDiagnosticsKind diagnostics;
    const char *module_map;
    const char *library_map;
    const char *entry;
    const char *profile;
    const char *target_manifest_path;
    const char *link_manifest_path;
    const char *abi_record_path;
    /* Header roots the ABI record header digests are verified against (R-FFI-0044). */
    const char *abi_header_dirs[64];
    size_t abi_header_dir_count;
    /* --rename-symbol OLD=NEW of the LLVM backends (RFrontendArtifactOptions). */
    const char *symbol_renames[64];
    size_t symbol_rename_count;
    /* --all-functions: lower every function, not only those the entry reaches. */
    bool all_functions;
    /* --opt-level=N: the LLVM pipeline of the program (2 when not given). */
    uint32_t optimization_level;
    bool optimization_level_given;
    const char **source_paths;
    size_t source_count;
    size_t source_capacity;
    RModuleMapEntry *module_entries;
    size_t module_entry_count;
    size_t module_entry_capacity;
    RModuleMapEntry *library_entries;
    size_t library_entry_count;
    size_t library_entry_capacity;
    uint8_t *target_manifest;
    size_t target_manifest_length;
    uint8_t *link_manifest;
    size_t link_manifest_length;
    uint8_t *abi_record;
    size_t abi_record_length;
    bool module_map_seen;
    bool library_map_seen;
    bool entry_seen;
    bool profile_seen;
    bool target_manifest_seen;
    bool link_manifest_seen;
    bool abi_record_seen;
    bool emit_seen;
    bool diagnostics_seen;
    bool deny_panic_alloc;
    /* R-FUNC-0025 (M24): translate the entry module in test mode. */
    bool test_mode;
    /* --bitcode-catalog FILE (B7.2): where the bitcode of the runtime and library C lies. */
    const char *bitcode_catalog_path;
    bool bitcode_catalog_seen;
    /* --profile-generate and --profile-use FILE (B7.2): profile-guided optimization. */
    bool profile_generate;
    const char *profile_use_path;
    bool profile_use_seen;
    /* --sanitize=address|thread (B8.3): the sanitizer the program is instrumented for. */
    RFrontendSanitizer sanitizer;
    /* --debug-info (B8.1): DWARF of the program. */
    bool debug_info;
    /* --coverage PREFIX (B8.2): line coverage of the R source. */
    const char *coverage_prefix;
    bool coverage_seen;
    /* --sysroot DIR (B9.1): the SDK the headers of imports are parsed in. */
    const char *sysroot;
    bool sysroot_seen;
    /* --library NAME (B10.1): the program is a library with this name. */
    const char *library_name;
    bool library_seen;
    /* --emit=shared-library (B10.1): the result, the linker, its inputs, the versions and the
       install name of the library. */
    const char *output;
    const char *linker;
    const char *link_inputs[256];
    size_t link_input_count;
    const char *library_version;
    const char *compatibility_version;
    const char *install_name;
    /* --abi-baseline FILE (B10.2): the ABI baseline of an earlier build of the library. */
    const char *abi_baseline;
} RCliOptions;

/* One line of a bitcode catalog, "SYMBOL<TAB>PATH": the bitcode module at PATH defines the C
   function SYMBOL. */
typedef struct RCliBitcodeEntry {
    const char *symbol;
    const char *path;
} RCliBitcodeEntry;

typedef struct RCliBitcodeModule {
    const char *path;
    uint8_t *bytes;
    size_t length;
} RCliBitcodeModule;

/* The catalog text, split in place, its entries sorted by symbol, and the modules read so far. */
typedef struct RCliBitcodeCatalog {
    uint8_t *text;
    size_t text_length;
    RCliBitcodeEntry *entries;
    size_t entry_count;
    RCliBitcodeModule *modules;
    size_t module_count;
    size_t module_capacity;
} RCliBitcodeCatalog;

static bool r_cli_status_is_resource_failure(RFrontendStatus status);
static RFrontendStatus r_cli_link_library(RFrontendContext *context,
                                          const struct RCliOptions *options,
                                          const RFrontendArtifactOptions *artifact_options);
static int r_cli_check_abi_baseline(RFrontendContext *context,
                                    const struct RCliOptions *options,
                                    const RFrontendArtifactOptions *artifact_options);

/* B10.1 (R-FFI-0064): a library name prefixes the C identifiers NAME_initialize and
   NAME_shutdown, so it is an ASCII identifier outside the reserved classes of C (no leading
   underscore) and no C keyword. */
static bool r_cli_library_name_valid(const char *name) {
    static const char *const keywords[] = {
        "auto",           "break",        "case",     "char",     "const",      "continue",
        "default",        "do",           "double",   "else",     "enum",       "extern",
        "float",          "for",          "goto",     "if",       "inline",     "int",
        "long",           "register",     "restrict", "return",   "short",      "signed",
        "sizeof",         "static",       "struct",   "switch",   "typedef",    "union",
        "unsigned",       "void",         "volatile", "while",    "_Alignas",   "_Alignof",
        "_Atomic",        "_Bool",        "_Complex", "_Generic", "_Imaginary", "_Noreturn",
        "_Static_assert", "_Thread_local"};
    size_t index;

    if ((name == NULL) || (name[0] == '\0') || (name[0] == '_') ||
        !(((name[0] >= 'a') && (name[0] <= 'z')) || ((name[0] >= 'A') && (name[0] <= 'Z')))) {
        return false;
    }
    for (index = 1U; name[index] != '\0'; ++index) {
        const char c = name[index];
        if (!(((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) ||
              ((c >= '0') && (c <= '9')) || (c == '_'))) {
            return false;
        }
    }
    for (index = 0U; index < sizeof(keywords) / sizeof(keywords[0]); ++index) {
        if (strcmp(name, keywords[index]) == 0) {
            return false;
        }
    }
    return true;
}

static void r_cli_usage(FILE *stream) {
    (void)fprintf(stream,
                  "usage: r-front "
                  "[--emit=tokens|cst|ast|hir|mir|interface|link-plan|bundle|llvm-ir|object|"
                  "abi-verifier|abi-inventory|abi-record|bindings|export-list|shared-library|"
                  "c-header|abi-baseline] "
                  "[--diagnostics=text|json] [--module-map FILE] [--library-map FILE] "
                  "[--entry MODULE[::FUNCTION]] "
                  "[--profile freestanding|allocation|hosted|hosted-thread|"
                  "hosted-native-async] [--deny-panic-alloc] [--test] [--target-manifest FILE] "
                  "[--link-manifest FILE] [--abi-record FILE] [--abi-header-dir DIR]... "
                  "[--sysroot DIR] "
                  "[--rename-symbol OLD=NEW]... [--all-functions] [--opt-level=0|1|2|3] "
                  "[--bitcode-catalog FILE] [--profile-generate | --profile-use FILE] "
                  "[--sanitize=address|thread] [--debug-info] [--coverage PREFIX] "
                  "[--library NAME] [--output FILE] [--linker CC] [--link-input FILE]... "
                  "[--library-version X.Y.Z] [--compatibility-version X.Y.Z] "
                  "[--install-name NAME] [--abi-baseline FILE] [--version] [FILE ...]\n");
}

/* The compiler and its backend; the backend check fails when the linked LLVM or its target
   machine disagrees with the target manifest (LLVM transition B0). */
static bool r_cli_print_version(void) {
    char version[32];
    char header_version[32];
    char message[512] = "LLVM version is unavailable";

    if (!r_frontend_backend_version(version, sizeof(version)) ||
        !r_frontend_backend_verify(message, sizeof(message)) ||
        !r_frontend_header_version(header_version, sizeof(header_version))) {
        (void)fprintf(stderr, "r-front: backend check failed: %s\n", message);
        return false;
    }
    (void)printf("r-front %s (Core %s)\nLLVM %s %s %s\nlibclang %s\n",
                 R_FRONTEND_VERSION,
                 R_FRONTEND_CORE_REVISION,
                 version,
                 r_frontend_backend_triple(),
                 r_frontend_backend_cpu(),
                 header_version);
    return true;
}

static bool r_cli_option_error(const char *message, const char *argument) {
    if (argument == NULL) {
        (void)fprintf(stderr, "r-front: %s\n", message);
    } else {
        (void)fprintf(stderr, "r-front: %s: %s\n", message, argument);
    }
    return false;
}

static bool r_file_writer(void *user_data, const char *bytes, size_t length) {
    FILE *stream = user_data;
    return (stream != NULL) && (fwrite(bytes, 1U, length, stream) == length);
}

static bool r_cli_add_path(RCliOptions *options, const char *path) {
    const char **replacement;
    char *path_copy;
    size_t capacity;
    if (options->source_count == options->source_capacity) {
        capacity = options->source_capacity == 0U ? 16U : options->source_capacity * 2U;
        if ((capacity < options->source_capacity) ||
            (capacity > (SIZE_MAX / sizeof(*options->source_paths)))) {
            return false;
        }
        replacement =
            r_cli_reallocate(options->source_paths, capacity * sizeof(*options->source_paths));
        if (replacement == NULL) {
            return false;
        }
        options->source_paths = replacement;
        options->source_capacity = capacity;
    }
    path_copy = r_cli_allocate(strlen(path) + 1U);
    if (path_copy == NULL) {
        return false;
    }
    (void)memcpy(path_copy, path, strlen(path) + 1U);
    options->source_paths[options->source_count] = path_copy;
    options->source_count += 1U;
    return true;
}

static void r_cli_destroy_options(RCliOptions *options) {
    size_t index;
    for (index = 0U; index < options->source_count; ++index) {
        r_cli_deallocate((void *)options->source_paths[index]);
    }
    for (index = 0U; index < options->module_entry_count; ++index) {
        r_cli_deallocate(options->module_entries[index].module_name);
        r_cli_deallocate(options->module_entries[index].path);
        r_cli_deallocate(options->module_entries[index].profile);
    }
    for (index = 0U; index < options->library_entry_count; ++index) {
        r_cli_deallocate(options->library_entries[index].module_name);
        r_cli_deallocate(options->library_entries[index].path);
        r_cli_deallocate(options->library_entries[index].profile);
    }
    r_cli_deallocate(options->source_paths);
    r_cli_deallocate(options->module_entries);
    r_cli_deallocate(options->library_entries);
    r_cli_deallocate(options->target_manifest);
    r_cli_deallocate(options->link_manifest);
    r_cli_deallocate(options->abi_record);
    (void)memset(options, 0, sizeof(*options));
}

/* OLD=NEW with two C identifiers. */
static bool r_cli_symbol_rename_is_valid(const char *pair) {
    size_t index = 0U;
    unsigned part;

    if (pair == NULL) {
        return false;
    }
    for (part = 0U; part < 2U; ++part) {
        const size_t start = index;
        while ((pair[index] == '_') || ((pair[index] >= 'a') && (pair[index] <= 'z')) ||
               ((pair[index] >= 'A') && (pair[index] <= 'Z')) ||
               ((index != start) && (pair[index] >= '0') && (pair[index] <= '9'))) {
            index += 1U;
        }
        if ((index == start) || (pair[index] != (part == 0U ? '=' : '\0'))) {
            return false;
        }
        index += 1U;
    }
    return true;
}

static bool r_cli_profile_is_valid(const char *profile) {
    return (strcmp(profile, "freestanding") == 0) || (strcmp(profile, "allocation") == 0) ||
           (strcmp(profile, "hosted") == 0) || (strcmp(profile, "hosted-thread") == 0) ||
           (strcmp(profile, "hosted-native-async") == 0);
}

static bool r_cli_set_emit(RCliOptions *options, REmitKind emit) {
    if (options->emit_seen) {
        return r_cli_option_error("duplicate option", "--emit");
    }
    options->emit_seen = true;
    options->emit = emit;
    return true;
}

static bool r_cli_set_diagnostics(RCliOptions *options, RDiagnosticsKind diagnostics) {
    if (options->diagnostics_seen) {
        return r_cli_option_error("duplicate option", "--diagnostics");
    }
    options->diagnostics_seen = true;
    options->diagnostics = diagnostics;
    return true;
}

static const char *r_cli_option_value(const char *argument, const char *name) {
    size_t name_length = strlen(name);
    if ((strncmp(argument, name, name_length) == 0) && (argument[name_length] == '=')) {
        return argument + name_length + 1U;
    }
    return NULL;
}

static bool r_cli_take_option_value(
    int argc, char **argv, int *index, const char *argument, const char *name, const char **value) {
    const char *inline_value = r_cli_option_value(argument, name);
    if (inline_value != NULL) {
        if (inline_value[0] == '\0') {
            return r_cli_option_error("empty option value", name);
        }
        *value = inline_value;
        return true;
    }
    *index += 1;
    if (*index >= argc) {
        return r_cli_option_error("missing option value", name);
    }
    *value = argv[*index];
    return true;
}

/* B10.1: a version of a Mach-O dylib, X[.Y[.Z]] with X <= 65535 and Y, Z <= 255. */
static bool r_cli_dylib_version_valid(const char *text) {
    static const unsigned long limits[3] = {65535UL, 255UL, 255UL};
    size_t part = 0U;
    const char *cursor = text;

    while (true) {
        unsigned long value = 0UL;
        size_t digits = 0U;
        while ((*cursor >= '0') && (*cursor <= '9') && (digits < 6U)) {
            value = value * 10UL + (unsigned long)(*cursor - '0');
            cursor += 1;
            digits += 1U;
        }
        if ((digits == 0U) || (value > limits[part])) {
            return false;
        }
        part += 1U;
        if (*cursor == '\0') {
            return true;
        }
        if ((*cursor != '.') || (part == 3U)) {
            return false;
        }
        cursor += 1;
    }
}

/* An option NAME VALUE or NAME=VALUE given at most once: sets *recognized when the argument
   is the option; false after a reported error. */
static bool r_cli_take_single_option(int argc,
                                     char **argv,
                                     int *index,
                                     const char *argument,
                                     const char *name,
                                     const char **value,
                                     bool *recognized) {
    if ((strcmp(argument, name) != 0) && (r_cli_option_value(argument, name) == NULL)) {
        return true;
    }
    if (*value != NULL) {
        return r_cli_option_error("duplicate option", name);
    }
    *recognized = true;
    return r_cli_take_option_value(argc, argv, index, argument, name, value);
}

static bool r_cli_parse_arguments(int argc, char **argv, RCliOptions *options) {
    int index;
    bool recognized = false;
    (void)memset(options, 0, sizeof(*options));
    options->emit = R_EMIT_AST;
    options->diagnostics = R_DIAGNOSTICS_TEXT;
    options->profile = "hosted-native-async";
    for (index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (strcmp(argument, "--emit=tokens") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_TOKENS)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=cst") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_CST)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=ast") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_AST)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=hir") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_HIR)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=mir") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_MIR)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=interface") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_INTERFACE)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=link-plan") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_LINK_PLAN)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=bundle") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_BUNDLE)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=llvm-ir") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_LLVM_IR)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=object") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_OBJECT)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=abi-verifier") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_ABI_VERIFIER)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=bindings") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_BINDINGS)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=export-list") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_EXPORT_LIST)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=shared-library") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_SHARED_LIBRARY)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=c-header") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_C_HEADER)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=abi-baseline") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_ABI_BASELINE)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=abi-record") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_ABI_RECORD)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=abi-inventory") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_ABI_INVENTORY)) {
                return false;
            }
        } else if (strcmp(argument, "--deny-panic-alloc") == 0) {
            if (options->deny_panic_alloc) {
                return r_cli_option_error("duplicate option", "--deny-panic-alloc");
            }
            options->deny_panic_alloc = true;
        } else if (strcmp(argument, "--test") == 0) {
            if (options->test_mode) {
                return r_cli_option_error("duplicate option", "--test");
            }
            options->test_mode = true;
        } else if ((strcmp(argument, "--abi-record") == 0) ||
                   (r_cli_option_value(argument, "--abi-record") != NULL)) {
            if (options->abi_record_seen) {
                return r_cli_option_error("duplicate option", "--abi-record");
            }
            options->abi_record_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--abi-record", &options->abi_record_path)) {
                return false;
            }
        } else if ((strcmp(argument, "--abi-header-dir") == 0) ||
                   (r_cli_option_value(argument, "--abi-header-dir") != NULL)) {
            const char *directory = NULL;

            if (options->abi_header_dir_count >=
                sizeof(options->abi_header_dirs) / sizeof(options->abi_header_dirs[0])) {
                return r_cli_option_error("too many header roots", "--abi-header-dir");
            }
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--abi-header-dir", &directory)) {
                return false;
            }
            options->abi_header_dirs[options->abi_header_dir_count] = directory;
            options->abi_header_dir_count += 1U;
        } else if ((strcmp(argument, "--sysroot") == 0) ||
                   (r_cli_option_value(argument, "--sysroot") != NULL)) {
            if (options->sysroot_seen) {
                return r_cli_option_error("duplicate option", "--sysroot");
            }
            options->sysroot_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--sysroot", &options->sysroot)) {
                return false;
            }
        } else if (strcmp(argument, "--diagnostics=text") == 0) {
            if (!r_cli_set_diagnostics(options, R_DIAGNOSTICS_TEXT)) {
                return false;
            }
        } else if (strcmp(argument, "--diagnostics=json") == 0) {
            if (!r_cli_set_diagnostics(options, R_DIAGNOSTICS_JSON)) {
                return false;
            }
        } else if ((strcmp(argument, "--module-map") == 0) ||
                   (r_cli_option_value(argument, "--module-map") != NULL)) {
            if (options->module_map_seen) {
                return r_cli_option_error("duplicate option", "--module-map");
            }
            options->module_map_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--module-map", &options->module_map)) {
                return false;
            }
        } else if ((strcmp(argument, "--library-map") == 0) ||
                   (r_cli_option_value(argument, "--library-map") != NULL)) {
            if (options->library_map_seen) {
                return r_cli_option_error("duplicate option", "--library-map");
            }
            options->library_map_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--library-map", &options->library_map)) {
                return false;
            }
        } else if ((strcmp(argument, "--entry") == 0) ||
                   (r_cli_option_value(argument, "--entry") != NULL)) {
            if (options->entry_seen) {
                return r_cli_option_error("duplicate option", "--entry");
            }
            options->entry_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--entry", &options->entry)) {
                return false;
            }
        } else if ((strcmp(argument, "--profile") == 0) ||
                   (r_cli_option_value(argument, "--profile") != NULL)) {
            if (options->profile_seen) {
                return r_cli_option_error("duplicate option", "--profile");
            }
            options->profile_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--profile", &options->profile)) {
                return false;
            }
            if (!r_cli_profile_is_valid(options->profile)) {
                return r_cli_option_error("unknown profile", options->profile);
            }
        } else if ((strcmp(argument, "--target-manifest") == 0) ||
                   (r_cli_option_value(argument, "--target-manifest") != NULL)) {
            if (options->target_manifest_seen) {
                return r_cli_option_error("duplicate option", "--target-manifest");
            }
            options->target_manifest_seen = true;
            if (!r_cli_take_option_value(argc,
                                         argv,
                                         &index,
                                         argument,
                                         "--target-manifest",
                                         &options->target_manifest_path)) {
                return false;
            }
        } else if ((strcmp(argument, "--link-manifest") == 0) ||
                   (r_cli_option_value(argument, "--link-manifest") != NULL)) {
            if (options->link_manifest_seen) {
                return r_cli_option_error("duplicate option", "--link-manifest");
            }
            options->link_manifest_seen = true;
            if (!r_cli_take_option_value(argc,
                                         argv,
                                         &index,
                                         argument,
                                         "--link-manifest",
                                         &options->link_manifest_path)) {
                return false;
            }
        } else if ((strcmp(argument, "--rename-symbol") == 0) ||
                   (r_cli_option_value(argument, "--rename-symbol") != NULL)) {
            const char *pair = NULL;
            if (options->symbol_rename_count ==
                sizeof(options->symbol_renames) / sizeof(options->symbol_renames[0])) {
                return r_cli_option_error("too many options", "--rename-symbol");
            }
            if (!r_cli_take_option_value(argc, argv, &index, argument, "--rename-symbol", &pair)) {
                return false;
            }
            if (!r_cli_symbol_rename_is_valid(pair)) {
                return r_cli_option_error("expected OLD=NEW C identifiers", pair);
            }
            options->symbol_renames[options->symbol_rename_count++] = pair;
        } else if (strcmp(argument, "--all-functions") == 0) {
            if (options->all_functions) {
                return r_cli_option_error("duplicate option", "--all-functions");
            }
            options->all_functions = true;
        } else if ((strcmp(argument, "--bitcode-catalog") == 0) ||
                   (r_cli_option_value(argument, "--bitcode-catalog") != NULL)) {
            if (options->bitcode_catalog_seen) {
                return r_cli_option_error("duplicate option", "--bitcode-catalog");
            }
            options->bitcode_catalog_seen = true;
            if (!r_cli_take_option_value(argc,
                                         argv,
                                         &index,
                                         argument,
                                         "--bitcode-catalog",
                                         &options->bitcode_catalog_path)) {
                return false;
            }
        } else if (strcmp(argument, "--profile-generate") == 0) {
            if (options->profile_generate) {
                return r_cli_option_error("duplicate option", "--profile-generate");
            }
            options->profile_generate = true;
        } else if ((strcmp(argument, "--profile-use") == 0) ||
                   (r_cli_option_value(argument, "--profile-use") != NULL)) {
            if (options->profile_use_seen) {
                return r_cli_option_error("duplicate option", "--profile-use");
            }
            options->profile_use_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--profile-use", &options->profile_use_path)) {
                return false;
            }
        } else if ((strcmp(argument, "--coverage") == 0) ||
                   (r_cli_option_value(argument, "--coverage") != NULL)) {
            if (options->coverage_seen) {
                return r_cli_option_error("duplicate option", "--coverage");
            }
            options->coverage_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--coverage", &options->coverage_prefix)) {
                return false;
            }
        } else if ((strcmp(argument, "--library") == 0) ||
                   (r_cli_option_value(argument, "--library") != NULL)) {
            if (options->library_seen) {
                return r_cli_option_error("duplicate option", "--library");
            }
            options->library_seen = true;
            if (!r_cli_take_option_value(
                    argc, argv, &index, argument, "--library", &options->library_name)) {
                return false;
            }
            if (!r_cli_library_name_valid(options->library_name)) {
                return r_cli_option_error("a library name is an ASCII C identifier that is no C "
                                          "keyword and does not begin with an underscore",
                                          options->library_name);
            }
        } else if ((strcmp(argument, "--link-input") == 0) ||
                   (r_cli_option_value(argument, "--link-input") != NULL)) {
            const char *input = NULL;
            if (options->link_input_count ==
                sizeof(options->link_inputs) / sizeof(options->link_inputs[0])) {
                return r_cli_option_error("too many link inputs", "--link-input");
            }
            if (!r_cli_take_option_value(argc, argv, &index, argument, "--link-input", &input)) {
                return false;
            }
            options->link_inputs[options->link_input_count++] = input;
        } else if (!r_cli_take_single_option(
                       argc, argv, &index, argument, "--output", &options->output, &recognized) ||
                   !r_cli_take_single_option(
                       argc, argv, &index, argument, "--linker", &options->linker, &recognized) ||
                   !r_cli_take_single_option(argc,
                                             argv,
                                             &index,
                                             argument,
                                             "--library-version",
                                             &options->library_version,
                                             &recognized) ||
                   !r_cli_take_single_option(argc,
                                             argv,
                                             &index,
                                             argument,
                                             "--compatibility-version",
                                             &options->compatibility_version,
                                             &recognized) ||
                   !r_cli_take_single_option(argc,
                                             argv,
                                             &index,
                                             argument,
                                             "--install-name",
                                             &options->install_name,
                                             &recognized) ||
                   !r_cli_take_single_option(argc,
                                             argv,
                                             &index,
                                             argument,
                                             "--abi-baseline",
                                             &options->abi_baseline,
                                             &recognized)) {
            return false;
        } else if (recognized) {
            recognized = false;
        } else if (strcmp(argument, "--debug-info") == 0) {
            if (options->debug_info) {
                return r_cli_option_error("duplicate option", "--debug-info");
            }
            options->debug_info = true;
        } else if (strncmp(argument, "--sanitize=", 11U) == 0) {
            const char *name = argument + 11U;
            if (options->sanitizer != R_FRONTEND_SANITIZER_NONE) {
                return r_cli_option_error("duplicate option", "--sanitize");
            }
            if (strcmp(name, "address") == 0) {
                options->sanitizer = R_FRONTEND_SANITIZER_ADDRESS;
            } else if (strcmp(name, "thread") == 0) {
                options->sanitizer = R_FRONTEND_SANITIZER_THREAD;
            } else {
                return r_cli_option_error("expected address or thread", argument);
            }
        } else if (strncmp(argument, "--opt-level=", 12U) == 0) {
            const char *level = argument + 12U;
            if (options->optimization_level_given) {
                return r_cli_option_error("duplicate option", "--opt-level");
            }
            if ((level[0] < '0') || (level[0] > '3') || (level[1] != '\0')) {
                return r_cli_option_error("expected 0, 1, 2 or 3", argument);
            }
            options->optimization_level = (uint32_t)(level[0] - '0');
            options->optimization_level_given = true;
        } else if ((strcmp(argument, "--help") == 0) || (strcmp(argument, "-h") == 0)) {
            r_cli_usage(stdout);
            exit(0);
        } else if (strcmp(argument, "--version") == 0) {
            exit(r_cli_print_version() ? 0 : 1);
        } else if (argument[0] == '-') {
            return r_cli_option_error("unknown option", argument);
        } else if (!r_cli_add_path(options, argument)) {
            return r_cli_option_error("unable to store source path", argument);
        }
    }
    if (((options->emit == R_EMIT_LINK_PLAN) || (options->emit == R_EMIT_BUNDLE)) &&
        (options->entry == NULL)) {
        return r_cli_option_error("--entry is required for selected emit", NULL);
    }
    if ((r_cli_emits_library(options->emit) || (options->abi_baseline != NULL)) &&
        (options->library_name == NULL)) {
        return r_cli_option_error("--library is required for selected emit", NULL);
    }
    if ((options->abi_baseline != NULL) && !r_cli_emits_library(options->emit)) {
        return r_cli_option_error("--abi-baseline requires a library emit", NULL);
    }
    if ((options->emit == R_EMIT_C_HEADER) && (options->sysroot == NULL)) {
        return r_cli_option_error("--emit=c-header requires --sysroot", NULL);
    }
    if ((options->emit == R_EMIT_SHARED_LIBRARY) &&
        ((options->output == NULL) || (options->linker == NULL) || (options->sysroot == NULL))) {
        return r_cli_option_error("--emit=shared-library requires --output, --linker and --sysroot",
                                  NULL);
    }
    if (((options->library_version != NULL) &&
         !r_cli_dylib_version_valid(options->library_version)) ||
        ((options->compatibility_version != NULL) &&
         !r_cli_dylib_version_valid(options->compatibility_version))) {
        return r_cli_option_error("a library version is X[.Y[.Z]] with X at most 65535 and Y and Z "
                                  "at most 255",
                                  NULL);
    }
    if ((options->source_count == 0U) && (options->module_map == NULL)) {
        return r_cli_option_error("no input sources", NULL);
    }
    return true;
}

static char *r_cli_copy_range(const char *begin, const char *end) {
    size_t length = (size_t)(end - begin);
    char *copy = r_cli_allocate(length + 1U);
    if (copy == NULL) {
        return NULL;
    }
    if (length != 0U) {
        (void)memcpy(copy, begin, length);
    }
    copy[length] = '\0';
    return copy;
}

static char *r_cli_directory_name(const char *path) {
    static const char current_directory[] = ".";
    const char *slash = strrchr(path, '/');
#if defined(_WIN32)
    const char *backslash = strrchr(path, '\\');
    if ((backslash != NULL) && ((slash == NULL) || (backslash > slash))) {
        slash = backslash;
    }
#endif
    if (slash == NULL) {
        return r_cli_copy_range(current_directory, current_directory + 1U);
    }
    if (slash == path) {
        return r_cli_copy_range(path, path + 1);
    }
    return r_cli_copy_range(path, slash);
}

static bool r_cli_path_is_absolute(const char *path) {
    if (path[0] == '/') {
        return true;
    }
#if defined(_WIN32)
    return (((path[0] >= 'A') && (path[0] <= 'Z')) || ((path[0] >= 'a') && (path[0] <= 'z'))) &&
           (path[1] == ':');
#else
    return false;
#endif
}

static char *r_cli_join_path(const char *directory, const char *path) {
    size_t directory_length;
    size_t path_length;
    char *joined;
    if (r_cli_path_is_absolute(path)) {
        return r_cli_copy_range(path, path + strlen(path));
    }
    directory_length = strlen(directory);
    path_length = strlen(path);
    if ((directory_length > SIZE_MAX - path_length - 2U)) {
        return NULL;
    }
    joined = r_cli_allocate(directory_length + path_length + 2U);
    if (joined == NULL) {
        return NULL;
    }
    (void)memcpy(joined, directory, directory_length);
    joined[directory_length] = '/';
    (void)memcpy(joined + directory_length + 1U, path, path_length + 1U);
    return joined;
}

static bool r_cli_add_map_entry(RModuleMapEntry **entries,
                                size_t *count,
                                size_t *capacity,
                                const char *map_kind,
                                const char *module_begin,
                                const char *module_end,
                                char *path,
                                char *profile) {
    RModuleMapEntry entry;
    RModuleMapEntry *replacement;
    size_t insert_index;
    size_t grown;

    (void)memset(&entry, 0, sizeof(entry));
    entry.module_name = r_cli_copy_range(module_begin, module_end);
    entry.path = path;
    entry.profile = profile;
    if (entry.module_name == NULL) {
        return false;
    }
    insert_index = 0U;
    while ((insert_index < *count) &&
           (strcmp((*entries)[insert_index].module_name, entry.module_name) < 0)) {
        insert_index += 1U;
    }
    if ((insert_index < *count) &&
        (strcmp((*entries)[insert_index].module_name, entry.module_name) == 0)) {
        (void)fprintf(stderr, "r-front: duplicate %s entry: %s\n", map_kind, entry.module_name);
        r_cli_deallocate(entry.module_name);
        return false;
    }
    if (*count == *capacity) {
        grown = *capacity == 0U ? 16U : *capacity * 2U;
        if ((grown < *capacity) || (grown > (SIZE_MAX / sizeof(**entries)))) {
            r_cli_deallocate(entry.module_name);
            return false;
        }
        replacement = r_cli_reallocate(*entries, grown * sizeof(**entries));
        if (replacement == NULL) {
            r_cli_deallocate(entry.module_name);
            return false;
        }
        *entries = replacement;
        *capacity = grown;
    }
    if (insert_index < *count) {
        (void)memmove(*entries + insert_index + 1U,
                      *entries + insert_index,
                      (*count - insert_index) * sizeof(**entries));
    }
    (*entries)[insert_index] = entry;
    *count += 1U;
    return true;
}

static bool r_cli_module_name_is_well_formed(const char *begin, const char *end) {
    const char *cursor;

    if ((begin == end) || (*begin == '.') || (end[-1] == '.')) {
        return false;
    }
    for (cursor = begin; cursor < end; ++cursor) {
        if (((*cursor == ' ') || (*cursor == '\t') || (*cursor == '\r') || (*cursor == '\n') ||
             (*cursor == '=') || (*cursor == ':')) ||
            ((*cursor == '.') && (cursor + 1 < end) && (cursor[1] == '.'))) {
            return false;
        }
    }
    return true;
}

/* Parses `MODULE = PATH` lines of a module map, or `MODULE = PATH [PROFILE]` lines of a
   library map whose modules are R sources of the standard library (R-MOD-0002). */
static bool r_cli_load_map_file(RCliOptions *options, const char *map_path, bool library) {
    FILE *stream;
    char line[8192];
    char *directory;
    const char *map_kind = library ? "library-map" : "module-map";
    RModuleMapEntry **entries = library ? &options->library_entries : &options->module_entries;
    size_t *count = library ? &options->library_entry_count : &options->module_entry_count;
    size_t *capacity = library ? &options->library_entry_capacity : &options->module_entry_capacity;
    bool success = true;
    size_t line_number = 0U;
    stream = fopen(map_path, "rb");
    if (stream == NULL) {
        (void)fprintf(stderr, "%s: %s\n", map_path, strerror(errno));
        return false;
    }
    directory = r_cli_directory_name(map_path);
    if (directory == NULL) {
        (void)fclose(stream);
        return false;
    }
    while (fgets(line, (int)sizeof(line), stream) != NULL) {
        char *line_end;
        char *comment = strchr(line, '#');
        char *equal;
        char *module_begin;
        char *module_end;
        char *path_begin;
        char *path_end;
        char *profile_begin = NULL;
        char *profile = NULL;
        char *joined;
        line_number += 1U;
        line_end = line + strlen(line);
        if ((line_end != line) && (line_end[-1] != '\n') && (feof(stream) == 0)) {
            (void)fprintf(stderr,
                          "r-front: %s:%lu: %s line is too long\n",
                          map_path,
                          (unsigned long)line_number,
                          map_kind);
            success = false;
            break;
        }
        if (comment != NULL) {
            *comment = '\0';
        }
        equal = strchr(line, '=');
        if (equal == NULL) {
            module_begin = line;
            while ((*module_begin == ' ') || (*module_begin == '\t') || (*module_begin == '\r') ||
                   (*module_begin == '\n')) {
                module_begin += 1;
            }
            if (*module_begin == '\0') {
                continue;
            }
            (void)fprintf(stderr,
                          "r-front: %s:%lu: expected MODULE = PATH\n",
                          map_path,
                          (unsigned long)line_number);
            success = false;
            break;
        }
        module_begin = line;
        while ((*module_begin == ' ') || (*module_begin == '\t')) {
            module_begin += 1;
        }
        module_end = equal;
        while ((module_end > module_begin) &&
               ((module_end[-1] == ' ') || (module_end[-1] == '\t'))) {
            module_end -= 1;
        }
        path_begin = equal + 1;
        while ((*path_begin == ' ') || (*path_begin == '\t')) {
            path_begin += 1;
        }
        path_end = path_begin + strlen(path_begin);
        while ((path_end > path_begin) && ((path_end[-1] == ' ') || (path_end[-1] == '\t') ||
                                           (path_end[-1] == '\r') || (path_end[-1] == '\n'))) {
            path_end -= 1;
        }
        *path_end = '\0';
        if (library) {
            /* The optional PROFILE column follows the path after blanks. */
            char *cursor = path_begin;
            while ((cursor < path_end) && (*cursor != ' ') && (*cursor != '\t')) {
                cursor += 1;
            }
            if (cursor < path_end) {
                *cursor = '\0';
                profile_begin = cursor + 1;
                while ((*profile_begin == ' ') || (*profile_begin == '\t')) {
                    profile_begin += 1;
                }
                path_end = cursor;
            }
        }
        if (!r_cli_module_name_is_well_formed(module_begin, module_end) ||
            (path_begin == path_end) ||
            ((profile_begin != NULL) && !r_cli_profile_is_valid(profile_begin))) {
            (void)fprintf(stderr,
                          "r-front: %s:%lu: invalid %s entry\n",
                          map_path,
                          (unsigned long)line_number,
                          library ? "MODULE = PATH [PROFILE]" : "MODULE = PATH");
            success = false;
            break;
        }
        if (library) {
            const char *profile_name = profile_begin == NULL ? "freestanding" : profile_begin;
            profile = r_cli_copy_range(profile_name, profile_name + strlen(profile_name));
            if (profile == NULL) {
                success = false;
                break;
            }
        }
        joined = r_cli_join_path(directory, path_begin);
        if ((joined == NULL) ||
            !r_cli_add_map_entry(
                entries, count, capacity, map_kind, module_begin, module_end, joined, profile)) {
            r_cli_deallocate(joined);
            r_cli_deallocate(profile);
            success = false;
            break;
        }
    }
    if (ferror(stream) != 0) {
        success = false;
    }
    r_cli_deallocate(directory);
    (void)fclose(stream);
    if (success && (*count == 0U)) {
        (void)fprintf(stderr, "r-front: %s: %s contains no entries\n", map_path, map_kind);
        success = false;
    }
    return success;
}

static RModuleMapEntry *
r_cli_find_map_entry(RModuleMapEntry *entries, size_t count, const char *module_name) {
    size_t lower = 0U;
    size_t upper = count;

    while (lower < upper) {
        size_t middle = lower + ((upper - lower) / 2U);
        int comparison = strcmp(entries[middle].module_name, module_name);
        if (comparison < 0) {
            lower = middle + 1U;
        } else if (comparison > 0) {
            upper = middle;
        } else {
            return &entries[middle];
        }
    }
    return NULL;
}

static RModuleMapEntry *r_cli_find_module_entry(RCliOptions *options, const char *module_name) {
    return r_cli_find_map_entry(options->module_entries, options->module_entry_count, module_name);
}

static char *r_cli_entry_module(const char *entry) {
    const char *separator;

    if ((entry == NULL) || (entry[0] == '\0')) {
        return NULL;
    }
    separator = strstr(entry, "::");
    if (separator == NULL) {
        return r_cli_copy_range(entry, entry + strlen(entry));
    }
    if ((separator == entry) || (separator[2] == '\0') || (strstr(separator + 2, "::") != NULL)) {
        return NULL;
    }
    return r_cli_copy_range(entry, separator);
}

static bool
r_cli_read_file_bytes(const char *option_name, const char *path, uint8_t **bytes, size_t *length) {
    FILE *stream;
    long file_length;

    *bytes = NULL;
    *length = 0U;
    stream = fopen(path, "rb");
    if (stream == NULL) {
        (void)fprintf(stderr, "r-front: %s %s: %s\n", option_name, path, strerror(errno));
        return false;
    }
    if ((fseek(stream, 0L, SEEK_END) != 0) || ((file_length = ftell(stream)) < 0L) ||
        (fseek(stream, 0L, SEEK_SET) != 0)) {
        (void)fprintf(stderr, "r-front: %s %s: unable to determine file size\n", option_name, path);
        (void)fclose(stream);
        return false;
    }
    *length = (size_t)file_length;
    if (*length == 0U) {
        (void)fprintf(stderr, "r-front: %s %s: manifest is empty\n", option_name, path);
        (void)fclose(stream);
        return false;
    }
    *bytes = r_cli_allocate(*length);
    if (*bytes == NULL) {
        (void)fclose(stream);
        return false;
    }
    if (fread(*bytes, 1U, *length, stream) != *length) {
        (void)fprintf(stderr, "r-front: %s %s: unable to read file\n", option_name, path);
        r_cli_deallocate(*bytes);
        *bytes = NULL;
        *length = 0U;
        (void)fclose(stream);
        return false;
    }
    if (fclose(stream) != 0) {
        r_cli_deallocate(*bytes);
        *bytes = NULL;
        *length = 0U;
        return false;
    }
    return true;
}

/* Reads a whole file when it exists; a missing file is not an error for a header root search. */
static bool r_cli_read_existing_file(const char *path, uint8_t **bytes, size_t *length) {
    FILE *stream;
    long file_length;

    *bytes = NULL;
    *length = 0U;
    stream = fopen(path, "rb");
    if (stream == NULL) {
        return false;
    }
    if ((fseek(stream, 0L, SEEK_END) != 0) || ((file_length = ftell(stream)) < 0L) ||
        (fseek(stream, 0L, SEEK_SET) != 0)) {
        (void)fclose(stream);
        return false;
    }
    *length = (size_t)file_length;
    *bytes = r_cli_allocate(*length == 0U ? 1U : *length);
    if (*bytes == NULL) {
        (void)fclose(stream);
        return false;
    }
    if ((*length != 0U) && (fread(*bytes, 1U, *length, stream) != *length)) {
        r_cli_deallocate(*bytes);
        *bytes = NULL;
        *length = 0U;
        (void)fclose(stream);
        return false;
    }
    (void)fclose(stream);
    return true;
}

static int r_cli_bitcode_compare(const void *left, const void *right) {
    return strcmp(((const RCliBitcodeEntry *)left)->symbol,
                  ((const RCliBitcodeEntry *)right)->symbol);
}

static void r_cli_bitcode_destroy(RCliBitcodeCatalog *catalog) {
    size_t index;

    for (index = 0U; index < catalog->module_count; ++index) {
        r_cli_deallocate(catalog->modules[index].bytes);
    }
    r_cli_deallocate(catalog->modules);
    r_cli_deallocate(catalog->entries);
    r_cli_deallocate(catalog->text);
    (void)memset(catalog, 0, sizeof(*catalog));
}

/* Reads the catalog and splits its lines in place; a line without a tab is a usage error. */
static bool r_cli_bitcode_load(const char *path, RCliBitcodeCatalog *catalog) {
    size_t index;
    size_t lines = 0U;
    char *text;

    (void)memset(catalog, 0, sizeof(*catalog));
    if (!r_cli_read_file_bytes("--bitcode-catalog", path, &catalog->text, &catalog->text_length)) {
        return false;
    }
    text = (char *)catalog->text;
    for (index = 0U; index < catalog->text_length; ++index) {
        lines += text[index] == '\n' ? 1U : 0U;
    }
    catalog->entries = r_cli_allocate((lines + 1U) * sizeof(*catalog->entries));
    if (catalog->entries == NULL) {
        r_cli_bitcode_destroy(catalog);
        return false;
    }
    index = 0U;
    while (index < catalog->text_length) {
        char *line = text + index;
        char *end = memchr(line, '\n', catalog->text_length - index);
        char *tab;
        if (end == NULL) {
            (void)fprintf(stderr, "r-front: --bitcode-catalog: the last line has no end\n");
            r_cli_bitcode_destroy(catalog);
            return false;
        }
        *end = '\0';
        index = (size_t)(end - text) + 1U;
        tab = strchr(line, '\t');
        if ((tab == NULL) || (tab == line) || (tab[1] == '\0')) {
            (void)fprintf(stderr, "r-front: --bitcode-catalog: malformed line: %s\n", line);
            r_cli_bitcode_destroy(catalog);
            return false;
        }
        *tab = '\0';
        catalog->entries[catalog->entry_count].symbol = line;
        catalog->entries[catalog->entry_count].path = tab + 1;
        catalog->entry_count += 1U;
    }
    qsort(catalog->entries, catalog->entry_count, sizeof(*catalog->entries), r_cli_bitcode_compare);
    return true;
}

/* RFrontendArtifactOptions.load_bitcode: a module is read once, the first time one of its
   symbols is asked for, and lives until the catalog is destroyed. */
static bool
r_cli_load_bitcode(void *user_data, const char *symbol, const uint8_t **data, size_t *length) {
    RCliBitcodeCatalog *catalog = user_data;
    const RCliBitcodeEntry key = {symbol, NULL};
    const RCliBitcodeEntry *entry = bsearch(&key,
                                            catalog->entries,
                                            catalog->entry_count,
                                            sizeof(*catalog->entries),
                                            r_cli_bitcode_compare);
    RCliBitcodeModule *module;
    size_t index;

    if (entry == NULL) {
        return false;
    }
    for (index = 0U; index < catalog->module_count; ++index) {
        if (strcmp(catalog->modules[index].path, entry->path) == 0) {
            *data = catalog->modules[index].bytes;
            *length = catalog->modules[index].length;
            return catalog->modules[index].bytes != NULL;
        }
    }
    if (catalog->module_count == catalog->module_capacity) {
        const size_t capacity =
            catalog->module_capacity == 0U ? 32U : catalog->module_capacity * 2U;
        RCliBitcodeModule *grown = r_cli_allocate(capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (catalog->module_count != 0U) {
            (void)memcpy(grown, catalog->modules, catalog->module_count * sizeof(*grown));
        }
        r_cli_deallocate(catalog->modules);
        catalog->modules = grown;
        catalog->module_capacity = capacity;
    }
    module = &catalog->modules[catalog->module_count++];
    module->path = entry->path;
    /* A module that cannot be read stays recorded, so it is not tried again. */
    if (!r_cli_read_existing_file(entry->path, &module->bytes, &module->length)) {
        module->bytes = NULL;
        module->length = 0U;
        return false;
    }
    *data = module->bytes;
    *length = module->length;
    return true;
}

/* R-FFI-0044: the ABI record header roots given with --abi-header-dir, searched in order. */
static bool r_cli_read_abi_header(void *user_data,
                                  const char *spelling,
                                  size_t spelling_length,
                                  uint8_t **bytes,
                                  size_t *length) {
    const RCliOptions *options = user_data;
    size_t index;

    *bytes = NULL;
    *length = 0U;
    for (index = 0U; index < options->abi_header_dir_count; ++index) {
        const char *directory = options->abi_header_dirs[index];
        const size_t directory_length = strlen(directory);
        char path[4096];

        if ((directory_length + 1U + spelling_length + 1U) > sizeof(path) ||
            (memchr(spelling, '\0', spelling_length) != NULL)) {
            continue;
        }
        (void)memcpy(path, directory, directory_length);
        path[directory_length] = '/';
        (void)memcpy(path + directory_length + 1U, spelling, spelling_length);
        path[directory_length + 1U + spelling_length] = '\0';
        if (r_cli_read_existing_file(path, bytes, length)) {
            return true;
        }
    }
    return false;
}

static void r_cli_release_abi_header(void *user_data, uint8_t *bytes) {
    (void)user_data;
    r_cli_deallocate(bytes);
}

static bool r_cli_read_source(RFrontendContext *context,
                              const char *path,
                              const char *library_profile,
                              RSourceId *source_id) {
    FILE *stream;
    long file_length;
    uint8_t *bytes;
    size_t length;
    RFrontendStatus status;

    stream = fopen(path, "rb");
    if (stream == NULL) {
        (void)fprintf(stderr, "%s: %s\n", path, strerror(errno));
        return false;
    }
    if ((fseek(stream, 0L, SEEK_END) != 0) || ((file_length = ftell(stream)) < 0L) ||
        (fseek(stream, 0L, SEEK_SET) != 0)) {
        (void)fprintf(stderr, "%s: unable to determine file size\n", path);
        (void)fclose(stream);
        return false;
    }
    length = (size_t)file_length;
    bytes = length == 0U ? NULL : r_cli_allocate(length);
    if ((length != 0U) && (bytes == NULL)) {
        (void)fclose(stream);
        return false;
    }
    if ((length != 0U) && (fread(bytes, 1U, length, stream) != length)) {
        (void)fprintf(stderr, "%s: unable to read source\n", path);
        r_cli_deallocate(bytes);
        (void)fclose(stream);
        return false;
    }
    if (fclose(stream) != 0) {
        r_cli_deallocate(bytes);
        return false;
    }
    status = library_profile == NULL
                 ? r_frontend_add_source(context, path, bytes, length, source_id)
                 : r_frontend_add_library_source(
                       context, path, bytes, length, library_profile, source_id);
    r_cli_deallocate(bytes);
    if (status != R_FRONTEND_OK) {
        (void)fprintf(stderr, "%s: frontend rejected source (%d)\n", path, (int)status);
        return false;
    }
    return true;
}

static bool
r_cli_load_module_entry(RFrontendContext *context, RModuleMapEntry *entry, RSourceId *source_id) {
    RFrontendStatus status;
    const char *declared_module;

    if (entry->loaded) {
        return true;
    }
    if (!r_cli_read_source(context, entry->path, entry->profile, source_id)) {
        return false;
    }
    status = r_frontend_scan_interface(context, *source_id);
    if (r_cli_status_is_resource_failure(status)) {
        return false;
    }
    declared_module = r_frontend_source_module_name(context, *source_id);
    if ((status == R_FRONTEND_OK) &&
        ((declared_module == NULL) || (strcmp(declared_module, entry->module_name) != 0))) {
        (void)fprintf(stderr,
                      "r-front: module-map key %s does not match declaration in %s\n",
                      entry->module_name,
                      entry->path);
        return false;
    }
    entry->loaded = true;
    return true;
}

/* Follows the imports of every loaded source: a module-map entry loads as a program module, a
   library-map entry loads as a standard module written in R (R-MOD-0002), a reserved module
   absent from both maps is a predeclared standard module, and any other absent module stays
   unloaded, so the semantic check reports it at its import (R-MOD-0007). */
static bool r_cli_load_reachable_modules(RFrontendContext *context, RCliOptions *options) {
    const bool indexed = (options->module_map != NULL) && (options->entry != NULL);
    RSourceId ignored_source = R_SOURCE_ID_INVALID;
    size_t source_index;

    if (indexed) {
        char *entry_module = r_cli_entry_module(options->entry);
        RModuleMapEntry *root;
        if (entry_module == NULL) {
            return r_cli_option_error("invalid --entry", options->entry);
        }
        root = r_cli_find_module_entry(options, entry_module);
        if (root == NULL) {
            (void)fprintf(
                stderr, "r-front: --entry module is absent from module map: %s\n", entry_module);
            r_cli_deallocate(entry_module);
            return false;
        }
        r_cli_deallocate(entry_module);
        if (!r_cli_load_module_entry(context, root, &ignored_source)) {
            return false;
        }
    }
    for (source_index = 0U; source_index < r_frontend_source_count(context); ++source_index) {
        RSourceId source_id = (RSourceId)(source_index + 1U);
        RFrontendStatus status = r_frontend_scan_interface(context, source_id);
        size_t import_index;
        if (r_cli_status_is_resource_failure(status)) {
            return false;
        }
        for (import_index = 0U; import_index < r_frontend_source_import_count(context, source_id);
             ++import_index) {
            const char *module_name =
                r_frontend_source_import_module(context, source_id, import_index);
            RModuleMapEntry *dependency;
            if (module_name == NULL) {
                return false;
            }
            dependency = indexed ? r_cli_find_module_entry(options, module_name) : NULL;
            if (dependency == NULL) {
                dependency = r_cli_find_map_entry(
                    options->library_entries, options->library_entry_count, module_name);
            }
            if (dependency == NULL) {
                continue;
            }
            if (!r_cli_load_module_entry(context, dependency, &ignored_source)) {
                return false;
            }
        }
    }
    return true;
}

static void r_cli_dump_text_diagnostics(const RFrontendContext *context) {
    size_t index;
    for (index = 0U; index < r_frontend_diagnostic_count(context); ++index) {
        const RDiagnostic *diagnostic = r_frontend_diagnostic(context, index);
        const char *name;
        uint32_t line;
        uint32_t column;
        if (diagnostic == NULL) {
            continue;
        }
        name = r_frontend_source_name(context, diagnostic->primary_span.source);
        r_frontend_source_position(context, diagnostic->primary_span, &line, &column);
        (void)fprintf(stderr,
                      "%s:%u:%u: %s %s [%s]: %s\n",
                      name == NULL ? "<source>" : name,
                      (unsigned int)line,
                      (unsigned int)column,
                      diagnostic->severity == R_DIAGNOSTIC_ERROR ? "error" : "warning",
                      diagnostic->code,
                      diagnostic->rule_id,
                      diagnostic->message);
    }
}

static bool r_cli_status_is_resource_failure(RFrontendStatus status) {
    return (status == R_FRONTEND_OUT_OF_MEMORY) || (status == R_FRONTEND_LIMIT_EXCEEDED) ||
           (status == R_FRONTEND_IO_ERROR) || (status == R_FRONTEND_INTERNAL_ERROR);
}

/*
 * Library R-SLIB-RSRC-0003 (M25): the providers that standard modules written in R import from
 * are entries of the library's link manifest, links.json next to the library map. When the
 * program uses such a module, its own manifest, if any, and that one are read as one manifest.
 */
static bool r_cli_merge_library_links(RCliOptions *options) {
    static const char prefix[] = "{\"schema\":\"r-link-manifest-0.1\",\"links\":[";
    static const char suffix[] = "]}";
    static const char file_name[] = "links.json";
    const char *separator = strrchr(options->library_map, '/');
    const size_t directory_length =
        separator == NULL ? 0U : (size_t)(separator - options->library_map) + 1U;
    char *path = r_cli_allocate(directory_length + sizeof(file_name));
    uint8_t *library = NULL;
    size_t library_length = 0U;
    size_t library_first = 0U;
    size_t library_last = 0U;
    size_t program_first = 0U;
    size_t program_last = 0U;
    bool merged = false;
    if (path == NULL) {
        return false;
    }
    (void)memcpy(path, options->library_map, directory_length);
    (void)memcpy(path + directory_length, file_name, sizeof(file_name));
    if (!r_cli_read_file_bytes("--library-map links", path, &library, &library_length)) {
        goto cleanup;
    }
    if (!r_frontend_link_manifest_links(library, library_length, &library_first, &library_last) ||
        ((options->link_manifest != NULL) &&
         !r_frontend_link_manifest_links(options->link_manifest,
                                         options->link_manifest_length,
                                         &program_first,
                                         &program_last))) {
        (void)fprintf(
            stderr, "r-front: %s or --link-manifest is not a valid link manifest\n", path);
        goto cleanup;
    }
    if ((options->link_manifest != NULL) &&
        r_frontend_link_manifests_share_name(
            options->link_manifest, options->link_manifest_length, library, library_length)) {
        (void)fprintf(stderr,
                      "r-front: --link-manifest names a logical name of the library link "
                      "manifest %s\n",
                      path);
        goto cleanup;
    }
    {
        const size_t program_count = program_last - program_first;
        const size_t library_count = library_last - library_first;
        const bool comma = (program_count != 0U) && (library_count != 0U);
        const size_t length = (sizeof(prefix) - 1U) + program_count + (comma ? 1U : 0U) +
                              library_count + (sizeof(suffix) - 1U);
        uint8_t *combined = r_cli_allocate(length);
        size_t offset = 0U;
        if (combined == NULL) {
            goto cleanup;
        }
        (void)memcpy(combined, prefix, sizeof(prefix) - 1U);
        offset += sizeof(prefix) - 1U;
        if (program_count != 0U) {
            (void)memcpy(combined + offset, options->link_manifest + program_first, program_count);
            offset += program_count;
        }
        if (comma) {
            combined[offset] = (uint8_t)',';
            offset += 1U;
        }
        (void)memcpy(combined + offset, library + library_first, library_count);
        offset += library_count;
        (void)memcpy(combined + offset, suffix, sizeof(suffix) - 1U);
        r_cli_deallocate(options->link_manifest);
        options->link_manifest = combined;
        options->link_manifest_length = length;
        merged = true;
    }
cleanup:
    r_cli_deallocate(library);
    r_cli_deallocate(path);
    return merged;
}

static int r_cli_run(RFrontendContext *context, RCliOptions *options) {
    size_t source_index;
    bool source_failure = false;
    REmitKind emit = options->emit;
    for (source_index = 0U; source_index < r_frontend_source_count(context); ++source_index) {
        RSourceId source_id = (RSourceId)(source_index + 1U);
        RFrontendStatus status = r_frontend_lex(context, source_id);
        if (r_cli_status_is_resource_failure(status)) {
            return 2;
        }
        source_failure = source_failure || (status == R_FRONTEND_INVALID_SOURCE);
    }
    if (emit != R_EMIT_TOKENS) {
        RFrontendStatus status = r_frontend_link_interfaces(context);
        if (r_cli_status_is_resource_failure(status)) {
            return 2;
        }
        for (source_index = 0U; source_index < r_frontend_source_count(context); ++source_index) {
            RSourceId source_id = (RSourceId)(source_index + 1U);
            RSyntaxNodeId cst_root;
            status = r_frontend_parse_cst(context, source_id, &cst_root);
            if (r_cli_status_is_resource_failure(status)) {
                return 2;
            }
            source_failure = source_failure || (status == R_FRONTEND_INVALID_SOURCE);
            if ((emit == R_EMIT_AST) || (emit == R_EMIT_HIR) || (emit == R_EMIT_MIR) ||
                (emit == R_EMIT_INTERFACE) || (emit == R_EMIT_LINK_PLAN) ||
                (emit == R_EMIT_BUNDLE) || r_cli_emits_program(emit) ||
                (emit == R_EMIT_ABI_VERIFIER) || (emit == R_EMIT_ABI_INVENTORY) ||
                (emit == R_EMIT_ABI_RECORD) || (emit == R_EMIT_BINDINGS)) {
                RAstNodeId ast_root;
                status = r_frontend_lower_ast(context, source_id, &ast_root);
                if (r_cli_status_is_resource_failure(status)) {
                    return 2;
                }
                source_failure = source_failure || (status == R_FRONTEND_NOT_LOWERABLE);
            }
        }
    }
    if ((emit == R_EMIT_HIR) || (emit == R_EMIT_MIR) || (emit == R_EMIT_INTERFACE) ||
        (emit == R_EMIT_LINK_PLAN) || (emit == R_EMIT_BUNDLE) || r_cli_emits_program(emit) ||
        (emit == R_EMIT_ABI_VERIFIER) || (emit == R_EMIT_ABI_INVENTORY) ||
        (emit == R_EMIT_ABI_RECORD) || (emit == R_EMIT_BINDINGS)) {
        RFrontendStatus status = r_frontend_analyze(context);
        if (r_cli_status_is_resource_failure(status)) {
            return 2;
        }
        source_failure = source_failure || (status == R_FRONTEND_INVALID_SOURCE) ||
                         (status == R_FRONTEND_NOT_LOWERABLE);
        if ((r_cli_emits_program(emit) || (emit == R_EMIT_LINK_PLAN) || (emit == R_EMIT_BUNDLE) ||
             (emit == R_EMIT_INTERFACE) || (emit == R_EMIT_ABI_VERIFIER) ||
             (emit == R_EMIT_ABI_INVENTORY) || (emit == R_EMIT_ABI_RECORD) ||
             (emit == R_EMIT_BINDINGS)) &&
            !source_failure) {
            if ((options->library_map != NULL) && r_frontend_uses_library_natives(context) &&
                !r_cli_merge_library_links(options)) {
                return 2;
            }
            status = r_frontend_resolve_links(
                context, options->link_manifest, options->link_manifest_length);
            if (status == R_FRONTEND_INVALID_SOURCE) {
                source_failure = true;
            } else if (status == R_FRONTEND_INVALID_ARGUMENT) {
                (void)fprintf(stderr, "r-front: --link-manifest is not a valid link manifest\n");
                return 2;
            } else if (r_cli_status_is_resource_failure(status)) {
                return 2;
            }
        }
        if ((r_cli_emits_program(emit) || (emit == R_EMIT_LINK_PLAN) || (emit == R_EMIT_BUNDLE) ||
             (emit == R_EMIT_INTERFACE) || (emit == R_EMIT_ABI_VERIFIER)) &&
            !source_failure) {
            /* R-FFI-0041: complete C aggregates are proven against the loaded ABI records. */
            if (options->abi_record != NULL) {
                status = r_frontend_load_abi_records(
                    context, options->abi_record, options->abi_record_length);
                if (status == R_FRONTEND_INVALID_ARGUMENT) {
                    (void)fprintf(stderr, "r-front: --abi-record is not a valid ABI record\n");
                    return 2;
                }
                if (r_cli_status_is_resource_failure(status)) {
                    return 2;
                }
            }
            status = r_frontend_verify_abi_records(
                context, options->target_manifest, options->target_manifest_length);
            if (status == R_FRONTEND_INVALID_SOURCE) {
                source_failure = true;
            } else if (r_cli_status_is_resource_failure(status)) {
                return 2;
            }
            if (!source_failure) {
                /* R-FFI-0044: the record's header digests are proven against this build. */
                RFrontendAbiHeaderSource header_source;

                header_source.read = r_cli_read_abi_header;
                header_source.release = r_cli_release_abi_header;
                header_source.user_data = (void *)options;
                status = r_frontend_verify_abi_record_headers(context, &header_source);
                if (status == R_FRONTEND_INVALID_SOURCE) {
                    source_failure = true;
                } else if (r_cli_status_is_resource_failure(status)) {
                    return 2;
                }
            }
            if (!source_failure && (options->sysroot != NULL)) {
                /* R-FFI-0042 (B9.1): the headers of the imports, parsed by libclang. */
                RFrontendHeaderOptions header_options;

                header_options.sysroot = options->sysroot;
                header_options.roots = options->abi_header_dirs;
                header_options.root_count = options->abi_header_dir_count;
                status = r_frontend_verify_headers(context, &header_options);
                if (status == R_FRONTEND_INVALID_ARGUMENT) {
                    (void)fprintf(stderr,
                                  "r-front: --sysroot is not the SDK of the target manifest, or "
                                  "libclang is not its C compiler release: %s\n",
                                  options->sysroot);
                    return 2;
                }
                if (status == R_FRONTEND_INVALID_SOURCE) {
                    source_failure = true;
                } else if (status != R_FRONTEND_OK) {
                    return 2;
                }
            }
        }
        if ((emit == R_EMIT_BINDINGS) && !source_failure &&
            (r_frontend_diagnostic_count(context) == 0U)) {
            /* B9.2: the declarations of a stub module's headers, as R source to review. */
            RFrontendHeaderOptions header_options;

            if (options->sysroot == NULL) {
                (void)fprintf(stderr, "r-front: --emit=bindings requires --sysroot\n");
                return 2;
            }
            header_options.sysroot = options->sysroot;
            header_options.roots = options->abi_header_dirs;
            header_options.root_count = options->abi_header_dir_count;
            status = r_frontend_emit_bindings(context, &header_options, r_file_writer, stdout);
            if (status == R_FRONTEND_INVALID_ARGUMENT) {
                (void)fprintf(stderr,
                              "r-front: --sysroot is not the SDK of the target manifest, or "
                              "libclang is not its C compiler release: %s\n",
                              options->sysroot);
                return 2;
            }
            if (status == R_FRONTEND_INVALID_SOURCE) {
                source_failure = true;
            } else if (status != R_FRONTEND_OK) {
                return 2;
            }
        }
        if ((emit == R_EMIT_ABI_RECORD) && !source_failure &&
            (r_frontend_diagnostic_count(context) == 0U)) {
            /* B9.1: the ABI records of the program's @abi blocks, derived by libclang. */
            RFrontendArtifactOptions record_options;
            RFrontendHeaderOptions header_options;

            if (options->sysroot == NULL) {
                (void)fprintf(stderr, "r-front: --emit=abi-record requires --sysroot\n");
                return 2;
            }
            (void)memset(&record_options, 0, sizeof(record_options));
            record_options.target_manifest = options->target_manifest;
            record_options.target_manifest_length = options->target_manifest_length;
            header_options.sysroot = options->sysroot;
            header_options.roots = options->abi_header_dirs;
            header_options.root_count = options->abi_header_dir_count;
            status = r_frontend_emit_abi_record(
                context, &record_options, &header_options, r_file_writer, stdout);
            if (status == R_FRONTEND_INVALID_ARGUMENT) {
                (void)fprintf(stderr,
                              "r-front: --sysroot is not the SDK of the target manifest, or "
                              "libclang is not its C compiler release: %s\n",
                              options->sysroot);
                return 2;
            }
            if (status == R_FRONTEND_INVALID_SOURCE) {
                source_failure = true;
            } else if (status != R_FRONTEND_OK) {
                return 2;
            }
        }
        if ((emit == R_EMIT_ABI_INVENTORY) && !source_failure &&
            (r_frontend_diagnostic_count(context) == 0U)) {
            RFrontendArtifactOptions inventory_options;

            (void)memset(&inventory_options, 0, sizeof(inventory_options));
            inventory_options.target_manifest = options->target_manifest;
            inventory_options.target_manifest_length = options->target_manifest_length;
            status =
                r_frontend_emit_abi_inventory(context, &inventory_options, r_file_writer, stdout);
            if (status != R_FRONTEND_OK) {
                return 2;
            }
        }
        if ((emit == R_EMIT_HIR) && (r_frontend_hir_root(context) != R_HIR_NODE_ID_INVALID)) {
            status = r_frontend_dump_hir(context, r_file_writer, stdout);
            if (status != R_FRONTEND_OK) {
                return 2;
            }
        }
        if ((emit == R_EMIT_ABI_VERIFIER) && !source_failure &&
            (r_frontend_diagnostic_count(context) == 0U)) {
            RFrontendArtifactOptions artifact_options;

            (void)memset(&artifact_options, 0, sizeof(artifact_options));
            artifact_options.entry = options->entry;
            artifact_options.profile = options->profile;
            artifact_options.target_manifest = options->target_manifest;
            artifact_options.target_manifest_length = options->target_manifest_length;
            artifact_options.link_manifest = options->link_manifest;
            artifact_options.link_manifest_length = options->link_manifest_length;
            status =
                r_frontend_emit_abi_verifier(context, &artifact_options, r_file_writer, stdout);
            if (status == R_FRONTEND_NOT_LOWERABLE) {
                return 1;
            }
            if (status != R_FRONTEND_OK) {
                return 2;
            }
        }
        if (r_cli_emits_program(emit) && !source_failure &&
            (r_frontend_diagnostic_count(context) == 0U)) {
            RFrontendArtifactOptions artifact_options;
            RCliBitcodeCatalog bitcode_catalog;

            (void)memset(&bitcode_catalog, 0, sizeof(bitcode_catalog));
            if ((options->library_name != NULL) && (r_frontend_entry_point_count(context) != 0U)) {
                (void)fprintf(stderr,
                              "r-front: R-DIAG-FLOW-001 [R-FUNC-0008]: a library has no main; "
                              "its exports are its entry points (R-FFI-0064)\n");
                return 1;
            }
            if ((options->library_name == NULL) && (r_frontend_entry_point_count(context) != 1U)) {
                (void)fprintf(stderr,
                              "r-front: R-DIAG-FLOW-001 [R-FUNC-0008]: a program requires exactly "
                              "one exported entry point: i32 main(), i32 main(const str[] args) "
                              "or their async forms\n");
                return 1;
            }
            status = r_frontend_lower_mir(context);
            if (status == R_FRONTEND_NOT_LOWERABLE) {
                /* Diagnostics of the MIR checks (await liveness) explain the rejection. */
                if (r_frontend_diagnostic_count(context) == 0U)
                    (void)fprintf(stderr,
                                  "r-front: R-DIAG-SLICE-001 [R-DIAG-0001]: the program is valid R "
                                  "but uses a construct outside the implemented MIR lowering\n");
                return 1;
            }
            if (status != R_FRONTEND_OK) {
                return 2;
            }
            (void)memset(&artifact_options, 0, sizeof(artifact_options));
            artifact_options.entry = options->entry;
            artifact_options.profile = options->profile;
            artifact_options.target_manifest = options->target_manifest;
            artifact_options.target_manifest_length = options->target_manifest_length;
            artifact_options.link_manifest = options->link_manifest;
            artifact_options.link_manifest_length = options->link_manifest_length;
            artifact_options.symbol_renames = options->symbol_renames;
            artifact_options.symbol_rename_count = options->symbol_rename_count;
            artifact_options.all_functions = options->all_functions;
            artifact_options.optimization_level =
                options->optimization_level_given ? options->optimization_level : 2U;
            if ((options->bitcode_catalog_path != NULL) &&
                !r_cli_bitcode_load(options->bitcode_catalog_path, &bitcode_catalog)) {
                return 2;
            }
            if (options->bitcode_catalog_path != NULL) {
                artifact_options.load_bitcode = r_cli_load_bitcode;
                artifact_options.bitcode_user_data = &bitcode_catalog;
            }
            artifact_options.profile_generate = options->profile_generate;
            artifact_options.profile_use = options->profile_use_path;
            artifact_options.sanitizer = options->sanitizer;
            artifact_options.debug_info = options->debug_info;
            artifact_options.coverage_prefix = options->coverage_prefix;
            artifact_options.library_name = options->library_name;
            if ((options->profile_generate || (options->profile_use_path != NULL)) &&
                ((artifact_options.optimization_level == 0U) ||
                 (options->profile_generate && (options->profile_use_path != NULL)))) {
                r_cli_bitcode_destroy(&bitcode_catalog);
                (void)fprintf(stderr,
                              "r-front: --profile-generate and --profile-use exclude each other "
                              "and --opt-level=0\n");
                return 2;
            }
            if ((options->library_name != NULL) &&
                (r_frontend_library_conflict(context, options->library_name) != NULL)) {
                r_cli_bitcode_destroy(&bitcode_catalog);
                (void)fprintf(stderr,
                              "r-front: R-DIAG-FFI-003 [R-FFI-0064]: an export of the program is "
                              "named %s%s, which the library defines itself\n",
                              options->library_name,
                              r_frontend_library_conflict(context, options->library_name));
                return 1;
            }
            if ((options->library_name != NULL) &&
                ((emit == R_EMIT_C_HEADER) || (emit == R_EMIT_ABI_BASELINE) ||
                 (options->abi_baseline != NULL))) {
                /* R-FFI-0065, R-FFI-0066: the header and the baseline need an API C declares. */
                char problem[512];
                if (r_frontend_library_api_problem(
                        context, options->library_name, problem, sizeof(problem)) != NULL) {
                    r_cli_bitcode_destroy(&bitcode_catalog);
                    (void)fprintf(stderr, "r-front: R-DIAG-FFI-003 [R-FFI-0065]: %s\n", problem);
                    return 1;
                }
            }
            if (options->abi_baseline != NULL) {
                const int verdict = r_cli_check_abi_baseline(context, options, &artifact_options);
                if (verdict != 0) {
                    r_cli_bitcode_destroy(&bitcode_catalog);
                    return verdict;
                }
            }
            if (emit == R_EMIT_EXPORT_LIST) {
                status =
                    r_frontend_emit_export_list(context, &artifact_options, r_file_writer, stdout);
            } else if (emit == R_EMIT_C_HEADER) {
                RFrontendHeaderOptions header_options;

                header_options.sysroot = options->sysroot;
                header_options.roots = options->abi_header_dirs;
                header_options.root_count = options->abi_header_dir_count;
                status = r_frontend_emit_c_header(context,
                                                  &artifact_options,
                                                  &header_options,
                                                  options->library_version,
                                                  r_file_writer,
                                                  stdout);
                if (status == R_FRONTEND_INVALID_SOURCE) {
                    r_cli_bitcode_destroy(&bitcode_catalog);
                    (void)fprintf(stderr,
                                  "r-front: R-DIAG-FFI-003 [R-FFI-0065]: the C header of the "
                                  "library does not compile for the target\n");
                    return 1;
                }
                if (status == R_FRONTEND_INVALID_ARGUMENT) {
                    r_cli_bitcode_destroy(&bitcode_catalog);
                    (void)fprintf(
                        stderr,
                        "r-front: --sysroot is not the SDK of the target manifest, or the "
                        "linked libclang is not its C compiler release\n");
                    return 2;
                }
            } else if (emit == R_EMIT_ABI_BASELINE) {
                status = r_frontend_emit_abi_baseline(
                    context,
                    &artifact_options,
                    options->library_version != NULL ? options->library_version : "1.0.0",
                    options->compatibility_version != NULL ? options->compatibility_version
                                                           : "1.0.0",
                    r_file_writer,
                    stdout);
            } else if (emit == R_EMIT_SHARED_LIBRARY) {
                status = r_cli_link_library(context, options, &artifact_options);
            } else {
                status = r_frontend_emit_llvm(context,
                                              &artifact_options,
                                              emit == R_EMIT_LLVM_IR ? R_FRONTEND_LLVM_IR
                                                                     : R_FRONTEND_LLVM_OBJECT,
                                              r_file_writer,
                                              stdout);
            }
            r_cli_bitcode_destroy(&bitcode_catalog);
            if (status == R_FRONTEND_NOT_LOWERABLE) {
                (void)fprintf(stderr,
                              "r-front: R-DIAG-SLICE-001 [R-DIAG-0001]: the program is valid R "
                              "but uses a construct outside the implemented LLVM lowering\n");
                return 1;
            }
            if (status == R_FRONTEND_LIMIT_EXCEEDED) {
                (void)fprintf(stderr,
                              "r-front: R-DIAG-LIMIT-001 [R-LIMIT-0003]: LLVM lowering exceeded a "
                              "translation limit\n");
                return 2;
            }
            if (status != R_FRONTEND_OK) {
                return 2;
            }
        }
        if (((emit == R_EMIT_MIR) || (emit == R_EMIT_INTERFACE) || (emit == R_EMIT_LINK_PLAN) ||
             (emit == R_EMIT_BUNDLE)) &&
            !source_failure && (r_frontend_diagnostic_count(context) == 0U)) {
            RFrontendArtifactOptions artifact_options;
            status = r_frontend_lower_mir(context);
            if (status == R_FRONTEND_NOT_LOWERABLE) {
                /* Diagnostics of the MIR checks (await liveness) explain the rejection. */
                if (r_frontend_diagnostic_count(context) == 0U)
                    (void)fprintf(stderr,
                                  "r-front: R-DIAG-SLICE-001 [R-DIAG-0001]: the program is valid R "
                                  "but uses a construct outside the implemented MIR lowering\n");
                return 1;
            }
            if (status != R_FRONTEND_OK) {
                return 2;
            }
            (void)memset(&artifact_options, 0, sizeof(artifact_options));
            artifact_options.entry = options->entry;
            artifact_options.profile = options->profile;
            artifact_options.target_manifest = options->target_manifest;
            artifact_options.target_manifest_length = options->target_manifest_length;
            artifact_options.link_manifest = options->link_manifest;
            artifact_options.link_manifest_length = options->link_manifest_length;
            if (emit == R_EMIT_MIR) {
                status = r_frontend_dump_mir(context, r_file_writer, stdout);
            } else if (emit == R_EMIT_INTERFACE) {
                status =
                    r_frontend_dump_interface(context, &artifact_options, r_file_writer, stdout);
            } else if (emit == R_EMIT_LINK_PLAN) {
                status =
                    r_frontend_dump_link_plan(context, &artifact_options, r_file_writer, stdout);
            } else {
                status = r_frontend_dump_bundle(context, &artifact_options, r_file_writer, stdout);
            }
            if (status == R_FRONTEND_NOT_LOWERABLE) {
                (void)fprintf(stderr,
                              "r-front: --entry does not name one exported definition: %s\n",
                              options->entry == NULL ? "<missing>" : options->entry);
                return 2;
            }
            if (status != R_FRONTEND_OK) {
                return 2;
            }
        }
        return (source_failure || (r_frontend_diagnostic_count(context) != 0U)) ? 1 : 0;
    }
    for (source_index = 0U; source_index < r_frontend_source_count(context); ++source_index) {
        RSourceId source_id = (RSourceId)(source_index + 1U);
        RFrontendStatus status;
        if (r_frontend_source_count(context) > 1U) {
            (void)printf("; source %s\n", r_frontend_source_name(context, source_id));
        }
        if (emit == R_EMIT_TOKENS) {
            status = r_frontend_dump_tokens(context, source_id, r_file_writer, stdout);
        } else if (emit == R_EMIT_CST) {
            status = r_frontend_dump_cst(context, source_id, r_file_writer, stdout);
        } else if (!source_failure) {
            status = r_frontend_dump_ast(context, source_id, r_file_writer, stdout);
        } else {
            continue;
        }
        if (status != R_FRONTEND_OK) {
            return 2;
        }
    }
    return (source_failure || (r_frontend_diagnostic_count(context) != 0U)) ? 1 : 0;
}

int main(int argc, char **argv) {
    RCliOptions options;
    RFrontendContext *context;
    size_t path_index;
    int result;
    if (!r_cli_parse_arguments(argc, argv, &options)) {
        r_cli_usage(stderr);
        r_cli_destroy_options(&options);
        return 2;
    }
    if ((options.module_map != NULL) && !r_cli_load_map_file(&options, options.module_map, false)) {
        r_cli_destroy_options(&options);
        return 2;
    }
    if ((options.library_map != NULL) &&
        !r_cli_load_map_file(&options, options.library_map, true)) {
        r_cli_destroy_options(&options);
        return 2;
    }
    if ((options.target_manifest_path != NULL) &&
        !r_cli_read_file_bytes("--target-manifest",
                               options.target_manifest_path,
                               &options.target_manifest,
                               &options.target_manifest_length)) {
        r_cli_destroy_options(&options);
        return 2;
    }
    /* R-CONF-G005: the values of a target manifest are those of a committed target; any other
       manifest would be silently ignored, so it is rejected before any artifact is produced. */
    if ((options.target_manifest_path != NULL) &&
        !r_frontend_target_manifest_supported(options.target_manifest,
                                              options.target_manifest_length,
                                              strcmp(options.profile, "freestanding") == 0)) {
        (void)fprintf(stderr,
                      "r-front: --target-manifest is not the target manifest of this "
                      "implementation for profile %s: %s\n",
                      options.profile,
                      options.target_manifest_path);
        r_cli_destroy_options(&options);
        return 2;
    }
    if ((options.link_manifest_path != NULL) &&
        !r_cli_read_file_bytes("--link-manifest",
                               options.link_manifest_path,
                               &options.link_manifest,
                               &options.link_manifest_length)) {
        r_cli_destroy_options(&options);
        return 2;
    }
    if ((options.abi_record_path != NULL) && !r_cli_read_file_bytes("--abi-record",
                                                                    options.abi_record_path,
                                                                    &options.abi_record,
                                                                    &options.abi_record_length)) {
        r_cli_destroy_options(&options);
        return 2;
    }
    context = r_frontend_create(NULL);
    if (context == NULL) {
        r_cli_destroy_options(&options);
        return 2;
    }
    if (r_frontend_set_profile(context, options.profile) != R_FRONTEND_OK) {
        (void)fprintf(stderr, "r-front: unknown profile: %s\n", options.profile);
        r_frontend_destroy(context);
        r_cli_destroy_options(&options);
        return 2;
    }
    if (options.deny_panic_alloc &&
        (r_frontend_set_deny_panic_alloc(context, true) != R_FRONTEND_OK)) {
        (void)fprintf(stderr, "r-front: cannot apply --deny-panic-alloc\n");
        r_frontend_destroy(context);
        r_cli_destroy_options(&options);
        return 2;
    }
    if (options.test_mode) {
        /* R-FUNC-0025: the module of --entry, or else the first source file, runs its tests. */
        char *test_module = options.entry != NULL ? r_cli_entry_module(options.entry) : NULL;
        const RFrontendStatus test_status = ((options.entry != NULL) && (test_module == NULL))
                                                ? R_FRONTEND_OUT_OF_MEMORY
                                                : r_frontend_set_test_mode(context, test_module);
        r_cli_deallocate(test_module);
        if (test_status != R_FRONTEND_OK) {
            (void)fprintf(stderr, "r-front: cannot apply --test\n");
            r_frontend_destroy(context);
            r_cli_destroy_options(&options);
            return 2;
        }
    }
    for (path_index = 0U; path_index < options.source_count; ++path_index) {
        RSourceId source_id = R_SOURCE_ID_INVALID;
        if (!r_cli_read_source(context, options.source_paths[path_index], NULL, &source_id)) {
            r_frontend_destroy(context);
            r_cli_destroy_options(&options);
            return 2;
        }
    }
    if ((options.module_map != NULL) && (options.entry == NULL)) {
        size_t module_index;
        for (module_index = 0U; module_index < options.module_entry_count; ++module_index) {
            RSourceId source_id = R_SOURCE_ID_INVALID;
            if (!r_cli_load_module_entry(
                    context, &options.module_entries[module_index], &source_id)) {
                r_frontend_destroy(context);
                r_cli_destroy_options(&options);
                return 2;
            }
        }
    }
    if ((((options.module_map != NULL) && (options.entry != NULL)) ||
         (options.library_map != NULL)) &&
        !r_cli_load_reachable_modules(context, &options)) {
        r_frontend_destroy(context);
        r_cli_destroy_options(&options);
        return 2;
    }
    result = r_cli_run(context, &options);
    if (options.diagnostics == R_DIAGNOSTICS_JSON) {
        if (r_frontend_dump_diagnostics_json(context, r_file_writer, stderr) != R_FRONTEND_OK) {
            result = 2;
        }
    } else {
        r_cli_dump_text_diagnostics(context);
    }
    {
        /* B9.1: what libclang said about the headers of the imports. */
        size_t report_length = 0U;
        const char *report = r_frontend_header_report(context, &report_length);

        if ((report != NULL) && ((fputs("r-front: header verification:\n", stderr) == EOF) ||
                                 (fwrite(report, 1U, report_length, stderr) != report_length))) {
            result = 2;
        }
    }
    r_frontend_destroy(context);
    r_cli_destroy_options(&options);
    return result;
}

/* B10.2 (R-FFI-0066): the library against the ABI baseline of an earlier build. A build with the
   compatibility version of the baseline keeps every export and type the baseline records; a
   higher one may break them; a lower one is refused. 0 when the build may proceed. */
static int r_cli_check_abi_baseline(RFrontendContext *context,
                                    const RCliOptions *options,
                                    const RFrontendArtifactOptions *artifact_options) {
    const char *compatibility =
        options->compatibility_version != NULL ? options->compatibility_version : "1.0.0";
    RFrontendAbiBaselineResult result;
    RFrontendStatus status;
    uint8_t *baseline = NULL;
    size_t length = 0U;

    if (!r_cli_read_file_bytes("--abi-baseline", options->abi_baseline, &baseline, &length)) {
        return 2;
    }
    status = r_frontend_check_abi_baseline(
        context,
        artifact_options,
        options->library_version != NULL ? options->library_version : "1.0.0",
        compatibility,
        baseline,
        length,
        &result);
    r_cli_deallocate(baseline);
    if (status != R_FRONTEND_OK) {
        return 2;
    }
    switch (result.verdict) {
    case R_FRONTEND_ABI_BASELINE_KEPT:
    case R_FRONTEND_ABI_BASELINE_ADMITTED:
        return 0;
    case R_FRONTEND_ABI_BASELINE_FOREIGN:
        (void)fprintf(stderr,
                      "r-front: R-DIAG-FFI-004 [R-FFI-0066]: %s is no ABI baseline of the library "
                      "%s for this target\n",
                      options->abi_baseline,
                      options->library_name);
        return 1;
    case R_FRONTEND_ABI_BASELINE_OLDER:
        (void)fprintf(stderr,
                      "r-front: R-DIAG-FFI-004 [R-FFI-0066]: the compatibility version %s is lower "
                      "than that of the ABI baseline %s\n",
                      compatibility,
                      options->abi_baseline);
        return 1;
    case R_FRONTEND_ABI_BASELINE_REMOVED:
    case R_FRONTEND_ABI_BASELINE_CHANGED:
    default:
        (void)fprintf(stderr,
                      "r-front: R-DIAG-FFI-004 [R-FFI-0066]: the %s %s of the ABI baseline %s %s "
                      "at compatibility version %s; a build that breaks it raises "
                      "--compatibility-version\n",
                      result.subject_is_type ? "type" : "export",
                      result.subject,
                      options->abi_baseline,
                      result.verdict == R_FRONTEND_ABI_BASELINE_REMOVED ? "is gone" : "changed",
                      compatibility);
        return 1;
    }
}

/* B10.1 (R-FFI-0064, R-CMAP-0031): the library linked by the toolchain's C compiler driver, with
   its versions and install name (by default @rpath/libNAME.dylib), and its exported symbols
   checked. */
static RFrontendStatus r_cli_link_library(RFrontendContext *context,
                                          const RCliOptions *options,
                                          const RFrontendArtifactOptions *artifact_options) {
    RFrontendLibraryLink link;
    RFrontendLibraryLinkResult result;
    RFrontendStatus status;
    char *install_name = NULL;

    (void)memset(&link, 0, sizeof(link));
    link.linker = options->linker;
    link.sysroot = options->sysroot;
    link.output = options->output;
    link.inputs = options->link_inputs;
    link.input_count = options->link_input_count;
    link.current_version = options->library_version != NULL ? options->library_version : "1.0.0";
    link.compatibility_version =
        options->compatibility_version != NULL ? options->compatibility_version : "1.0.0";
    if (options->install_name != NULL) {
        link.install_name = options->install_name;
    } else {
        const size_t length = strlen(options->library_name) + 20U;
        install_name = r_cli_allocate(length);
        if (install_name == NULL) {
            return R_FRONTEND_OUT_OF_MEMORY;
        }
        (void)snprintf(install_name, length, "@rpath/lib%s.dylib", options->library_name);
        link.install_name = install_name;
    }
    status = r_frontend_link_library(context, artifact_options, &link, &result);
    if (result.linker_unproved) {
        (void)fprintf(stderr,
                      "r-front: --linker is not the C compiler of the target manifest "
                      "(toolchain.c_compiler_build)\n");
    } else if (result.link_failed) {
        (void)fprintf(stderr, "r-front: the link of the library failed\n");
    } else if (result.missing || result.extra) {
        (void)fprintf(stderr,
                      result.missing ? "r-front: R-DIAG-LINK-003 [R-CMAP-0031]: the linked library "
                                       "does not export %s\n"
                                     : "r-front: R-DIAG-LINK-003 [R-CMAP-0031]: the linked library "
                                       "exports %s, which is no export of the program\n",
                      result.symbol);
    }
    r_cli_deallocate(install_name);
    return status;
}
