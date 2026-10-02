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
    R_EMIT_C17,
    R_EMIT_ABI_VERIFIER,
    R_EMIT_C17_BRIDGE,
    R_EMIT_ABI_INVENTORY
} REmitKind;

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
} RCliOptions;

static bool r_cli_status_is_resource_failure(RFrontendStatus status);

static void r_cli_usage(FILE *stream) {
    (void)fprintf(stream,
                  "usage: r-front "
                  "[--emit=tokens|cst|ast|hir|mir|interface|link-plan|bundle|c17|"
                  "abi-verifier|c17-bridge|abi-inventory] "
                  "[--diagnostics=text|json] [--module-map FILE] [--library-map FILE] "
                  "[--entry MODULE[::FUNCTION]] "
                  "[--profile freestanding|allocation|hosted|hosted-thread|"
                  "hosted-native-async] [--deny-panic-alloc] [--test] [--target-manifest FILE] "
                  "[--link-manifest FILE] [--abi-record FILE] [--abi-header-dir DIR]... "
                  "[FILE ...]\n");
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

static bool r_cli_parse_arguments(int argc, char **argv, RCliOptions *options) {
    int index;
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
        } else if (strcmp(argument, "--emit=c17") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_C17)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=abi-verifier") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_ABI_VERIFIER)) {
                return false;
            }
        } else if (strcmp(argument, "--emit=c17-bridge") == 0) {
            if (!r_cli_set_emit(options, R_EMIT_C17_BRIDGE)) {
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
        } else if ((strcmp(argument, "--help") == 0) || (strcmp(argument, "-h") == 0)) {
            r_cli_usage(stdout);
            exit(0);
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
        (void)fprintf(stderr, "r-front: %s or --link-manifest is not a valid link manifest\n", path);
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
        const size_t length =
            (sizeof(prefix) - 1U) + program_count + (comma ? 1U : 0U) + library_count + (sizeof(suffix) - 1U);
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
                (emit == R_EMIT_BUNDLE) || (emit == R_EMIT_C17) || (emit == R_EMIT_ABI_VERIFIER) ||
                (emit == R_EMIT_C17_BRIDGE) || (emit == R_EMIT_ABI_INVENTORY)) {
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
        (emit == R_EMIT_LINK_PLAN) || (emit == R_EMIT_BUNDLE) || (emit == R_EMIT_C17) ||
        (emit == R_EMIT_ABI_VERIFIER) || (emit == R_EMIT_C17_BRIDGE) ||
        (emit == R_EMIT_ABI_INVENTORY)) {
        RFrontendStatus status = r_frontend_analyze(context);
        if (r_cli_status_is_resource_failure(status)) {
            return 2;
        }
        source_failure = source_failure || (status == R_FRONTEND_INVALID_SOURCE) ||
                         (status == R_FRONTEND_NOT_LOWERABLE);
        if (((emit == R_EMIT_C17) || (emit == R_EMIT_LINK_PLAN) || (emit == R_EMIT_BUNDLE) ||
             (emit == R_EMIT_INTERFACE) || (emit == R_EMIT_ABI_VERIFIER) ||
             (emit == R_EMIT_C17_BRIDGE) || (emit == R_EMIT_ABI_INVENTORY)) &&
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
        if (((emit == R_EMIT_C17) || (emit == R_EMIT_LINK_PLAN) || (emit == R_EMIT_BUNDLE) ||
             (emit == R_EMIT_INTERFACE) || (emit == R_EMIT_ABI_VERIFIER) ||
             (emit == R_EMIT_C17_BRIDGE)) &&
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
        if (((emit == R_EMIT_ABI_VERIFIER) || (emit == R_EMIT_C17_BRIDGE)) && !source_failure &&
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
                emit == R_EMIT_ABI_VERIFIER
                    ? r_frontend_emit_abi_verifier(
                          context, &artifact_options, r_file_writer, stdout)
                    : r_frontend_emit_c17_bridge(context, &artifact_options, r_file_writer, stdout);
            if (status == R_FRONTEND_NOT_LOWERABLE) {
                return 1;
            }
            if (status != R_FRONTEND_OK) {
                return 2;
            }
        }
        if ((emit == R_EMIT_C17) && !source_failure &&
            (r_frontend_diagnostic_count(context) == 0U)) {
            RFrontendArtifactOptions artifact_options;

            if (r_frontend_entry_point_count(context) != 1U) {
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
            status =
                r_frontend_emit_c17_with_options(context, &artifact_options, r_file_writer, stdout);
            if (status == R_FRONTEND_NOT_LOWERABLE) {
                (void)fprintf(stderr,
                              "r-front: R-DIAG-SLICE-001 [R-DIAG-0001]: the program is valid R "
                              "but uses a construct outside the implemented C17 lowering\n");
                return 1;
            }
            if (status == R_FRONTEND_LIMIT_EXCEEDED) {
                (void)fprintf(stderr,
                              "r-front: R-DIAG-LIMIT-001 [R-LIMIT-0003]: C17 lowering exceeded a "
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
        const RFrontendStatus test_status =
            ((options.entry != NULL) && (test_module == NULL))
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
    r_frontend_destroy(context);
    r_cli_destroy_options(&options);
    return result;
}
