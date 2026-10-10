#include "abi.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define R_ABI_CHECK(condition)                                                                     \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (0)

/* Removes `word` (a whole token) from text wherever it occurs. */
static void r_abi_test_remove_word(char *text, const char *word) {
    const size_t length = strlen(word);
    char *found = text;

    while ((found = strstr(found, word)) != NULL) {
        const bool starts = (found == text) || (found[-1] == ' ') || (found[-1] == '(');
        const bool ends = (found[length] == ' ') || (found[length] == ',') ||
                          (found[length] == ')') || (found[length] == '\0');
        if (starts && ends) {
            (void)memmove(found, found + length, strlen(found + length) + 1U);
        } else {
            found += length;
        }
    }
}

/* Removes "WORD(...)" or "WORD N" tokens with their argument. */
static void r_abi_test_remove_argument(char *text, const char *word, bool parenthesized) {
    const size_t length = strlen(word);
    char *found;

    while ((found = strstr(text, word)) != NULL) {
        char *end = found + length;
        if (parenthesized) {
            int depth = 0;
            while (*end != '\0') {
                depth += *end == '(' ? 1 : (*end == ')' ? -1 : 0);
                end += 1;
                if (depth == 0) {
                    break;
                }
            }
        } else {
            while (*end == ' ') {
                end += 1;
            }
            while ((*end >= '0') && (*end <= '9')) {
                end += 1;
            }
        }
        (void)memmove(found, end, strlen(end) + 1U);
    }
}

static void r_abi_test_collapse_spaces(char *text) {
    char *read = text;
    char *write = text;
    bool space = false;

    while (*read != '\0') {
        if (*read == ' ') {
            space = true;
        } else {
            if (space && (write != text) && (write[-1] != '(') && (*read != ',') &&
                (*read != ')')) {
                *write++ = ' ';
            }
            space = false;
            *write++ = *read;
        }
        read += 1;
    }
    *write = '\0';
}

/* The lowering clang wrote, in the normalized form r_llvm_abi_render_function writes. */
static void r_abi_test_normalize(const char *lowered, char *text, size_t capacity) {
    static const char *const attributes[] = {
        "noundef", "dead_on_unwind", "writable", "nonnull", "noalias", "readonly", "nocapture"};
    char *cursor;
    size_t index;

    (void)snprintf(text, capacity, "%s", lowered);
    r_abi_test_remove_argument(text, "align ", false);
    r_abi_test_remove_argument(text, "dereferenceable(", true);
    r_abi_test_remove_argument(text, "captures(", true);
    for (index = 0U; index < sizeof(attributes) / sizeof(attributes[0]); ++index) {
        r_abi_test_remove_word(text, attributes[index]);
    }
    while ((cursor = strstr(text, "sret(")) != NULL) {
        char *end = cursor + 4;
        int depth = 0;
        while (*end != '\0') {
            depth += *end == '(' ? 1 : (*end == ')' ? -1 : 0);
            end += 1;
            if (depth == 0) {
                break;
            }
        }
        (void)memmove(cursor + 4, end, strlen(end) + 1U);
    }
    r_abi_test_collapse_spaces(text);
    /* "ptr sret" is the result slot, "ptr dead_on_return" an argument copy. */
    while ((cursor = strstr(text, "ptr sret")) != NULL) {
        (void)memmove(cursor, cursor + 4, strlen(cursor + 4) + 1U);
    }
    while ((cursor = strstr(text, "ptr dead_on_return")) != NULL) {
        (void)memcpy(cursor, "indirect", 8U);
        (void)memmove(cursor + 8, cursor + 18, strlen(cursor + 18) + 1U);
    }
    /* A result extension is written before its type; arguments write it after. */
    if (strncmp(text, "zeroext i1 ", 11U) == 0) {
        (void)memcpy(text, "i1 zeroext ", 11U);
    }
    /* A homogeneous result is returned as its structure type. */
    if (strncmp(text, "%struct.", 8U) == 0) {
        cursor = strchr(text, ' ');
        if (cursor != NULL) {
            (void)memmove(text + 3, cursor, strlen(cursor) + 1U);
            (void)memcpy(text, "hfa", 3U);
        }
    }
}

/* Our "hfa N x T" result compares by kind only: the argument forms check N and T. */
static void r_abi_test_shorten_homogeneous(char *text) {
    if (strncmp(text, "hfa ", 4U) == 0) {
        char *open = strstr(text, " (");
        if (open != NULL) {
            (void)memmove(text + 3, open, strlen(open) + 1U);
        }
    }
}

/* The classifier lowers every runtime surface function as clang does (B3). */
static void r_abi_test_surface_functions(void) {
    const RLlvmTypeTable table = r_llvm_surface_table();
    size_t index;
    size_t mismatches = 0U;

    for (index = 0U; index < r_llvm_surface_function_count(); ++index) {
        const RLlvmSurfaceFunction *function = r_llvm_surface_function_at(index);
        char expected[2048];
        char actual[2048];

        if (function == NULL) {
            failures += 1;
            continue;
        }
        r_abi_test_normalize(function->lowered, expected, sizeof(expected));
        if (!r_llvm_abi_render_function(&table, function->type, actual, sizeof(actual))) {
            (void)fprintf(stderr, "%s: not classified (clang: %s)\n", function->name, expected);
            mismatches += 1U;
            continue;
        }
        r_abi_test_shorten_homogeneous(actual);
        if (strcmp(expected, actual) != 0) {
            if (mismatches < 20U) {
                (void)fprintf(
                    stderr, "%s:\n  clang: %s\n  ours:  %s\n", function->name, expected, actual);
            }
            mismatches += 1U;
        }
    }
    if (mismatches != 0U) {
        (void)fprintf(
            stderr, "%zu of %zu functions differ\n", mismatches, r_llvm_surface_function_count());
    }
    R_ABI_CHECK(mismatches == 0U);
}

/* B10-1: nested aggregates. segment {point from, to} (point {double x, y}) is a homogeneous
   aggregate of four doubles, passed in d0-d3 as clang passes it; {point, float} is not one. */
static const RLlvmSurfaceType r_abi_test_types[] = {
    {R_LLVM_SURFACE_FLOAT, 0U, 8U, 8U, 0U, 0U, 0U, "double"},
    {R_LLVM_SURFACE_STRUCT, 0U, 16U, 8U, 0U, 2U, 0U, "point"},
    {R_LLVM_SURFACE_STRUCT, 0U, 32U, 8U, 0U, 2U, 2U, "segment"},
    {R_LLVM_SURFACE_FLOAT, 0U, 4U, 4U, 0U, 0U, 0U, "float"},
    {R_LLVM_SURFACE_STRUCT, 0U, 24U, 8U, 0U, 2U, 4U, "mixed"},
};
static const RLlvmSurfaceField r_abi_test_fields[] = {
    {"x", 0U, 1U},
    {"y", 8U, 1U},
    {"from", 0U, 2U},
    {"to", 16U, 2U},
    {"point", 0U, 2U},
    {"weight", 16U, 4U},
};

static const RLlvmSurfaceType *r_abi_test_type(const void *context, uint32_t index) {
    (void)context;
    return ((index >= 1U) && (index <= sizeof(r_abi_test_types) / sizeof(r_abi_test_types[0])))
               ? &r_abi_test_types[index - 1U]
               : NULL;
}

static const RLlvmSurfaceField *r_abi_test_field(const void *context, uint32_t index) {
    (void)context;
    return (index < sizeof(r_abi_test_fields) / sizeof(r_abi_test_fields[0]))
               ? &r_abi_test_fields[index]
               : NULL;
}

static void r_abi_test_nested_homogeneous(void) {
    RLlvmTypeTable table;
    RLlvmAbiValue value;

    table.type = r_abi_test_type;
    table.field = r_abi_test_field;
    table.parameter = NULL;
    table.context = NULL;
    R_ABI_CHECK(r_llvm_abi_classify_argument(&table, 2U, &value) && value.homogeneous &&
                (value.count == 2U) && (value.element == R_LLVM_ABI_ELEMENT_DOUBLE));
    R_ABI_CHECK(r_llvm_abi_classify_argument(&table, 3U, &value) && value.homogeneous &&
                (value.abi_class == R_LLVM_ABI_COERCE) && (value.count == 4U) &&
                (value.element == R_LLVM_ABI_ELEMENT_DOUBLE));
    R_ABI_CHECK(r_llvm_abi_classify_result(&table, 3U, &value) && value.homogeneous &&
                (value.count == 4U));
    R_ABI_CHECK(r_llvm_abi_classify_argument(&table, 5U, &value) && !value.homogeneous &&
                (value.abi_class == R_LLVM_ABI_INDIRECT));
}

int main(void) {
    r_abi_test_surface_functions();
    r_abi_test_nested_homogeneous();
    if (failures != 0) {
        (void)fprintf(stderr, "%d C ABI checks failed\n", failures);
        return 1;
    }
    return 0;
}
