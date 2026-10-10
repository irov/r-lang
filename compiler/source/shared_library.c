/* B10.1 (R-FFI-0064, R-CMAP-0031): a library as a shared library of the target.
 *
 * The exported symbols of a library are its C exports (R-FFI-0007) and NAME_initialize and
 * NAME_shutdown, which the emitter defines. r_frontend_link_library writes the object and that
 * list, runs the C compiler driver of the target manifest's toolchain (proved by the digest of
 * the manifest) as the linker with the list as the only exported symbols, and then reads the
 * defined external symbols of the result with the toolchain's llvm-nm: the export set is a
 * verified property of the link, not an attribute the backend inserts (R-CMAP-0031). */

#include "frontend_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

/* The C name of every export of the program, in symbol order. */
static bool r_library_export_name(const RFrontendContext *context,
                                  size_t index,
                                  const char **bytes,
                                  size_t *length) {
    const RSemanticSymbol *symbol = &context->semantic_symbols[index];
    const RInternEntry *name;

    if ((symbol->kind != R_SEMANTIC_SYMBOL_FUNCTION) || !symbol->is_extern_c || symbol->is_import ||
        !symbol->has_definition || (symbol->c_name_intern_id == 0U) ||
        ((size_t)symbol->c_name_intern_id > context->intern_count)) {
        return false;
    }
    name = &context->intern_entries[(size_t)symbol->c_name_intern_id - 1U];
    *bytes = (const char *)name->bytes;
    *length = name->length;
    return true;
}

static bool
r_library_name_is(const char *bytes, size_t length, const char *name, const char *suffix) {
    const size_t name_length = strlen(name);
    const size_t suffix_length = strlen(suffix);

    return (length == name_length + suffix_length) && (memcmp(bytes, name, name_length) == 0) &&
           (memcmp(bytes + name_length, suffix, suffix_length) == 0);
}

const char *r_frontend_library_conflict(const RFrontendContext *context, const char *name) {
    static const char *const suffixes[] = {"_initialize", "_shutdown"};
    size_t index;

    if ((context == NULL) || (name == NULL)) {
        return NULL;
    }
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const char *bytes;
        size_t length;
        size_t suffix;
        if (!r_library_export_name(context, index, &bytes, &length)) {
            continue;
        }
        for (suffix = 0U; suffix < sizeof(suffixes) / sizeof(suffixes[0]); ++suffix) {
            if (r_library_name_is(bytes, length, name, suffixes[suffix])) {
                return suffixes[suffix];
            }
        }
    }
    return NULL;
}

RFrontendStatus r_frontend_emit_export_list(const RFrontendContext *context,
                                            const RFrontendArtifactOptions *options,
                                            RFrontendWriteFn writer,
                                            void *user_data) {
    const char *prefix = r_frontend_target_global_prefix();
    size_t index;

    if ((context == NULL) || (options == NULL) || (options->library_name == NULL) ||
        (writer == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    if (!writer(user_data, prefix, strlen(prefix)) ||
        !writer(user_data, options->library_name, strlen(options->library_name)) ||
        !writer(user_data, "_initialize\n", 12U) || !writer(user_data, prefix, strlen(prefix)) ||
        !writer(user_data, options->library_name, strlen(options->library_name)) ||
        !writer(user_data, "_shutdown\n", 10U)) {
        return R_FRONTEND_IO_ERROR;
    }
    for (index = 0U; index < context->semantic_symbol_count; ++index) {
        const char *bytes;
        size_t length;
        if (r_library_export_name(context, index, &bytes, &length) &&
            (!writer(user_data, prefix, strlen(prefix)) || !writer(user_data, bytes, length) ||
             !writer(user_data, "\n", 1U))) {
            return R_FRONTEND_IO_ERROR;
        }
    }
    return R_FRONTEND_OK;
}

/* ---- the link ------------------------------------------------------------------------- */

typedef struct RLibraryText {
    RFrontendContext *context;
    char *bytes;
    size_t length;
    size_t capacity;
} RLibraryText;

static bool r_library_text_writer(void *user_data, const char *bytes, size_t length) {
    RLibraryText *text = user_data;

    if (!r_grow_array(text->context,
                      (void **)&text->bytes,
                      &text->capacity,
                      1U,
                      text->length + length + 1U)) {
        return false;
    }
    (void)memcpy(text->bytes + text->length, bytes, length);
    text->length += length;
    text->bytes[text->length] = '\0';
    return true;
}

static bool r_library_file_writer(void *user_data, const char *bytes, size_t length) {
    return fwrite(bytes, 1U, length, (FILE *)user_data) == length;
}

/* The digest of the linker equals toolchain.c_compiler_build of the manifest. */
static bool r_library_linker_proved(RFrontendContext *context, const char *linker) {
    static const char hex[] = "0123456789abcdef";
    const char *expected = r_frontend_target_c_compiler_build();
    char actual[7U + 64U + 1U] = "sha256:";
    uint8_t digest[32];
    uint8_t *bytes = NULL;
    size_t length = 0U;
    size_t capacity = 0U;
    FILE *file;
    bool proved = false;

    if ((expected == NULL) || ((file = fopen(linker, "rb")) == NULL)) {
        return false;
    }
    for (;;) {
        size_t read;
        if (!r_grow_array(context, (void **)&bytes, &capacity, 1U, length + 65536U)) {
            break;
        }
        read = fread(bytes + length, 1U, 65536U, file);
        length += read;
        if (read < 65536U) {
            proved = ferror(file) == 0;
            break;
        }
    }
    (void)fclose(file);
    if (proved) {
        r_sha256_digest(bytes, length, digest);
        for (size_t index = 0U; index < 32U; ++index) {
            actual[7U + index * 2U] = hex[digest[index] >> 4U];
            actual[8U + index * 2U] = hex[digest[index] & 15U];
        }
        actual[sizeof(actual) - 1U] = '\0';
        proved = strcmp(actual, expected) == 0;
    }
    r_context_free(context, bytes);
    return proved;
}

/* Runs argv and waits; standard output goes to `output` when given. True when it exits 0. */
static bool r_library_run(char *const argv[], int output) {
    posix_spawn_file_actions_t actions;
    pid_t child;
    int status = 0;
    bool started;

    if (posix_spawn_file_actions_init(&actions) != 0) {
        return false;
    }
    if ((output >= 0) && (posix_spawn_file_actions_adddup2(&actions, output, 1) != 0)) {
        (void)posix_spawn_file_actions_destroy(&actions);
        return false;
    }
    started = posix_spawn(&child, argv[0], &actions, NULL, argv, environ) == 0;
    (void)posix_spawn_file_actions_destroy(&actions);
    if (!started) {
        return false;
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            return false;
        }
    }
    return WIFEXITED(status) && (WEXITSTATUS(status) == 0);
}

/* A path in the temporary directory: DIRECTORY/NAME. */
static char *r_library_path(RFrontendContext *context, const char *directory, const char *name) {
    const size_t length = strlen(directory) + strlen(name) + 2U;
    char *path = r_context_allocate(context, length);

    if (path != NULL) {
        (void)snprintf(path, length, "%s/%s", directory, name);
    }
    return path;
}

/* Compares the defined external symbols that llvm-nm printed with the export list; reports the
   first difference in `problem`. */
static bool r_library_symbols_match(const char *printed,
                                    const char *expected,
                                    RFrontendLibraryLinkResult *result) {
    const char *line;

    for (line = expected; *line != '\0';) {
        const char *end = strchr(line, '\n');
        const size_t length = (end == NULL) ? strlen(line) : (size_t)(end - line);
        const char *search = printed;
        bool found = false;
        while ((search != NULL) && (*search != '\0')) {
            const char *search_end = strchr(search, '\n');
            const size_t search_length =
                (search_end == NULL) ? strlen(search) : (size_t)(search_end - search);
            found = found || ((search_length == length) && (memcmp(search, line, length) == 0));
            search = (search_end == NULL) ? NULL : search_end + 1;
        }
        if (!found && (length != 0U)) {
            result->missing = true;
            (void)snprintf(result->symbol, sizeof(result->symbol), "%.*s", (int)length, line);
            return false;
        }
        line = (end == NULL) ? line + length : end + 1;
    }
    for (line = printed; *line != '\0';) {
        const char *end = strchr(line, '\n');
        const size_t length = (end == NULL) ? strlen(line) : (size_t)(end - line);
        const char *search = expected;
        bool found = false;
        while ((search != NULL) && (*search != '\0')) {
            const char *search_end = strchr(search, '\n');
            const size_t search_length =
                (search_end == NULL) ? strlen(search) : (size_t)(search_end - search);
            found = found || ((search_length == length) && (memcmp(search, line, length) == 0));
            search = (search_end == NULL) ? NULL : search_end + 1;
        }
        if (!found && (length != 0U)) {
            result->extra = true;
            (void)snprintf(result->symbol, sizeof(result->symbol), "%.*s", (int)length, line);
            return false;
        }
        line = (end == NULL) ? line + length : end + 1;
    }
    return true;
}

RFrontendStatus r_frontend_link_library(RFrontendContext *context,
                                        const RFrontendArtifactOptions *options,
                                        const RFrontendLibraryLink *link,
                                        RFrontendLibraryLinkResult *result) {
    char directory[] = "/tmp/r-library-XXXXXX";
    const char *temporary = getenv("TMPDIR");
    char *template_path = NULL;
    char *object_path = NULL;
    char *list_path = NULL;
    char *symbols_path = NULL;
    char *nm_path = NULL;
    char *exported_option = NULL;
    char **argv = NULL;
    RLibraryText list = {0};
    RLibraryText printed = {0};
    RFrontendStatus status = R_FRONTEND_INTERNAL_ERROR;
    FILE *file = NULL;
    size_t argc = 0U;
    char *workspace = NULL;

    if ((context == NULL) || (options == NULL) || (link == NULL) || (result == NULL) ||
        (options->library_name == NULL) || (link->linker == NULL) || (link->output == NULL) ||
        (link->sysroot == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    (void)memset(result, 0, sizeof(*result));
    list.context = context;
    printed.context = context;
    if (!r_library_linker_proved(context, link->linker)) {
        result->linker_unproved = true;
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    /* A private directory for the object, the list and the symbols of the result. */
    if ((temporary != NULL) && (temporary[0] != '\0')) {
        template_path = r_library_path(context, temporary, "r-library-XXXXXX");
        workspace = template_path == NULL ? NULL : mkdtemp(template_path);
    } else {
        workspace = mkdtemp(directory);
    }
    if (workspace == NULL) {
        r_context_free(context, template_path);
        return R_FRONTEND_IO_ERROR;
    }
    object_path = r_library_path(context, workspace, "library.o");
    list_path = r_library_path(context, workspace, "exports.txt");
    symbols_path = r_library_path(context, workspace, "symbols.txt");
    if ((object_path == NULL) || (list_path == NULL) || (symbols_path == NULL)) {
        goto cleanup;
    }
    if ((file = fopen(object_path, "wb")) == NULL) {
        status = R_FRONTEND_IO_ERROR;
        goto cleanup;
    }
    status =
        r_frontend_emit_llvm(context, options, R_FRONTEND_LLVM_OBJECT, r_library_file_writer, file);
    if ((fclose(file) != 0) && (status == R_FRONTEND_OK)) {
        status = R_FRONTEND_IO_ERROR;
    }
    file = NULL;
    if (status != R_FRONTEND_OK) {
        goto cleanup;
    }
    status = r_frontend_emit_export_list(context, options, r_library_text_writer, &list);
    if (status != R_FRONTEND_OK) {
        goto cleanup;
    }
    status = R_FRONTEND_IO_ERROR;
    if (((file = fopen(list_path, "wb")) == NULL) ||
        (fwrite(list.bytes, 1U, list.length, file) != list.length)) {
        goto cleanup;
    }
    if (fclose(file) != 0) {
        file = NULL;
        goto cleanup;
    }
    file = NULL;
    {
        static const char option[] = "-Wl,-exported_symbols_list,";
        const size_t length = sizeof(option) + strlen(list_path);
        exported_option = r_context_allocate(context, length);
        if (exported_option != NULL) {
            (void)snprintf(exported_option, length, "%s%s", option, list_path);
        }
    }
    argv = r_context_allocate(context, (link->input_count + 32U) * sizeof(*argv));
    if ((exported_option == NULL) || (argv == NULL)) {
        status = R_FRONTEND_OUT_OF_MEMORY;
        goto cleanup;
    }
    argv[argc++] = (char *)link->linker;
    argv[argc++] = (char *)"-isysroot";
    argv[argc++] = (char *)link->sysroot;
    argv[argc++] = (char *)"-dynamiclib";
    argv[argc++] = (char *)"-o";
    argv[argc++] = (char *)link->output;
    argv[argc++] = object_path;
    for (size_t index = 0U; index < link->input_count; ++index) {
        argv[argc++] = (char *)link->inputs[index];
    }
    argv[argc++] = exported_option;
    argv[argc++] = (char *)"-Wl,-dead_strip";
    argv[argc++] = (char *)"-install_name";
    argv[argc++] = (char *)link->install_name;
    argv[argc++] = (char *)"-current_version";
    argv[argc++] = (char *)link->current_version;
    argv[argc++] = (char *)"-compatibility_version";
    argv[argc++] = (char *)link->compatibility_version;
    if (options->sanitizer == R_FRONTEND_SANITIZER_ADDRESS) {
        argv[argc++] = (char *)"-fsanitize=address";
    } else if (options->sanitizer == R_FRONTEND_SANITIZER_THREAD) {
        argv[argc++] = (char *)"-fsanitize=thread";
    }
    if (options->coverage_prefix != NULL) {
        argv[argc++] = (char *)"--coverage";
    }
    if (options->profile_generate) {
        argv[argc++] = (char *)"-fprofile-instr-generate";
    }
    argv[argc] = NULL;
    if (!r_library_run(argv, -1)) {
        result->link_failed = true;
        status = R_FRONTEND_IO_ERROR;
        goto cleanup;
    }
    /* The defined external symbols of the result, with the toolchain's llvm-nm. */
    {
        const char *slash = strrchr(link->linker, '/');
        const size_t directory_length = slash == NULL ? 0U : (size_t)(slash - link->linker);
        char *nm_arguments[6];
        int output;
        bool listed;
        nm_path = r_context_allocate(context, directory_length + 16U);
        if (nm_path == NULL) {
            status = R_FRONTEND_OUT_OF_MEMORY;
            goto cleanup;
        }
        (void)snprintf(nm_path,
                       directory_length + 16U,
                       "%.*s%sllvm-nm",
                       (int)directory_length,
                       link->linker,
                       slash == NULL ? "" : "/");
        nm_arguments[0] = nm_path;
        nm_arguments[1] = (char *)"--extern-only";
        nm_arguments[2] = (char *)"--defined-only";
        nm_arguments[3] = (char *)"--just-symbol-name";
        nm_arguments[4] = (char *)link->output;
        nm_arguments[5] = NULL;
        output = open(symbols_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
        if (output < 0) {
            goto cleanup;
        }
        listed = r_library_run(nm_arguments, output);
        (void)close(output);
        if (!listed || ((file = fopen(symbols_path, "rb")) == NULL)) {
            result->link_failed = true;
            goto cleanup;
        }
        for (;;) {
            char buffer[4096];
            const size_t read = fread(buffer, 1U, sizeof(buffer), file);
            if ((read != 0U) && !r_library_text_writer(&printed, buffer, read)) {
                goto cleanup;
            }
            if (read < sizeof(buffer)) {
                break;
            }
        }
        (void)fclose(file);
        file = NULL;
    }
    status = r_library_symbols_match(printed.bytes == NULL ? "" : printed.bytes, list.bytes, result)
                 ? R_FRONTEND_OK
                 : R_FRONTEND_INVALID_ARGUMENT;

cleanup:
    if (file != NULL) {
        (void)fclose(file);
    }
    if (object_path != NULL) {
        (void)unlink(object_path);
    }
    if (list_path != NULL) {
        (void)unlink(list_path);
    }
    if (symbols_path != NULL) {
        (void)unlink(symbols_path);
    }
    (void)rmdir(workspace);
    r_context_free(context, object_path);
    r_context_free(context, list_path);
    r_context_free(context, symbols_path);
    r_context_free(context, nm_path);
    r_context_free(context, exported_option);
    r_context_free(context, argv);
    r_context_free(context, list.bytes);
    r_context_free(context, printed.bytes);
    r_context_free(context, template_path);
    return status;
}

/* ---- B10.2: the API of the library in C and its ABI baseline -------------------------- */

const char *r_frontend_library_api_problem(const RFrontendContext *context,
                                           const char *name,
                                           char *buffer,
                                           size_t capacity) {
    if ((context == NULL) || (name == NULL) || (buffer == NULL) || (capacity == 0U)) {
        return NULL;
    }
    return r_c_abi_library_problem(context, name, buffer, capacity) ? buffer : NULL;
}

RFrontendStatus r_frontend_emit_abi_baseline(const RFrontendContext *context,
                                             const RFrontendArtifactOptions *options,
                                             const char *version,
                                             const char *compatibility_version,
                                             RFrontendWriteFn writer,
                                             void *user_data) {
    if ((context == NULL) || (options == NULL) || (options->library_name == NULL) ||
        (version == NULL) || (compatibility_version == NULL) || (writer == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    return r_c_abi_write_library_baseline(
        context, options->library_name, version, compatibility_version, writer, user_data);
}

/* R-FFI-0066: the baseline of this build, compared with the earlier one. */
RFrontendStatus r_frontend_check_abi_baseline(const RFrontendContext *context,
                                              const RFrontendArtifactOptions *options,
                                              const char *version,
                                              const char *compatibility_version,
                                              const uint8_t *baseline,
                                              size_t baseline_length,
                                              RFrontendAbiBaselineResult *result) {
    RLibraryText current;
    RFrontendStatus status;

    if ((context == NULL) || (options == NULL) || (options->library_name == NULL) ||
        (version == NULL) || (compatibility_version == NULL) || (baseline == NULL) ||
        (result == NULL)) {
        return R_FRONTEND_INVALID_ARGUMENT;
    }
    (void)memset(&current, 0, sizeof(current));
    current.context = (RFrontendContext *)context;
    status = r_c_abi_write_library_baseline(context,
                                            options->library_name,
                                            version,
                                            compatibility_version,
                                            r_library_text_writer,
                                            &current);
    if ((status == R_FRONTEND_OK) && !r_abi_baseline_compare((RFrontendContext *)context,
                                                             baseline,
                                                             baseline_length,
                                                             (const uint8_t *)current.bytes,
                                                             current.length,
                                                             result)) {
        status = R_FRONTEND_INTERNAL_ERROR;
    }
    r_context_free((RFrontendContext *)context, current.bytes);
    return status;
}
