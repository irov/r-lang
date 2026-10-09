#include "emit_internal.h"

#include <stdlib.h>
#include <string.h>

/* Keys independent of the order of the sources (B6-2). Semantic ids of types and symbols follow
   the order in which the sources were given, so a name or a report built from an id would make
   the module and the program depend on that order. Each type, symbol and source instead gets the
   one-based rank of a text that names it whatever the order: a type its MIR spelling, a symbol
   its module, name, type and declaration position, a source its module name. */

typedef struct RLlvmKeyText {
    uint32_t id;
    char *bytes;
    size_t length;
    size_t capacity;
} RLlvmKeyText;

typedef struct RLlvmKeyWriter {
    RLlvmEmitter *emitter;
    RLlvmKeyText *text;
} RLlvmKeyWriter;

static bool r_llvm_key_write(void *user_data, const char *bytes, size_t length) {
    RLlvmKeyWriter *writer = user_data;
    RLlvmKeyText *text = writer->text;

    if (length > text->capacity - text->length) {
        size_t capacity = text->capacity == 0U ? 64U : text->capacity;
        char *grown;
        while (capacity - text->length < length) {
            capacity *= 2U;
        }
        grown = r_llvm_allocate(writer->emitter, capacity);
        if (grown == NULL) {
            return false;
        }
        if (text->length != 0U) {
            (void)memcpy(grown, text->bytes, text->length);
        }
        r_llvm_free(writer->emitter, text->bytes);
        text->bytes = grown;
        text->capacity = capacity;
    }
    if (length != 0U) {
        (void)memcpy(text->bytes + text->length, bytes, length);
        text->length += length;
    }
    return true;
}

static bool r_llvm_key_append(RLlvmKeyWriter *writer, const char *bytes, size_t length) {
    return r_llvm_key_write(writer, bytes, length);
}

static bool r_llvm_key_number(RLlvmKeyWriter *writer, uint64_t value) {
    char digits[24];
    size_t length = 0U;

    do {
        digits[sizeof(digits) - 1U - length] = (char)('0' + (char)(value % 10U));
        value /= 10U;
        length += 1U;
    } while (value != 0U);
    return r_llvm_key_append(writer, &digits[sizeof(digits) - length], length);
}

static const char *r_llvm_key_module(const RLlvmEmitter *emitter, RSourceId id) {
    const RSource *source = r_get_source_const(emitter->frontend, id);
    return (source == NULL) || (source->module_name == NULL) ? "" : source->module_name;
}

static const char *r_llvm_key_display(const RLlvmEmitter *emitter, RSourceId id) {
    const RSource *source = r_get_source_const(emitter->frontend, id);
    return (source == NULL) || (source->display_name == NULL) ? "" : source->display_name;
}

/* A symbol: the module that declares it, its name, its type, its result type and the place of
   its name, which keeps apart the instances of a generic function by their types and objects of
   one name in different blocks by their position. */
static bool r_llvm_symbol_text(RLlvmKeyWriter *writer, RSymbolId id) {
    const RFrontendContext *context = writer->emitter->frontend;
    const RSemanticSymbol *symbol = &context->semantic_symbols[(size_t)id - 1U];
    const char *module = r_llvm_key_module(writer->emitter, symbol->module_source);
    const char *display = r_llvm_key_display(writer->emitter, symbol->name_span.source);

    if (!r_llvm_key_append(writer, module, strlen(module)) ||
        !r_llvm_key_append(writer, "\x01", 1U)) {
        return false;
    }
    if ((symbol->name_intern_id != 0U) &&
        ((size_t)symbol->name_intern_id <= context->intern_count)) {
        const RInternEntry *name = &context->intern_entries[symbol->name_intern_id - 1U];
        if (!r_llvm_key_append(writer, (const char *)name->bytes, name->length)) {
            return false;
        }
    }
    return r_llvm_key_append(writer, "\x01", 1U) &&
           r_llvm_key_number(writer, (uint64_t)symbol->kind) &&
           r_llvm_key_append(writer, "\x01", 1U) &&
           r_mir_write_type_text(context, symbol->type, r_llvm_key_write, writer) &&
           r_llvm_key_append(writer, "\x01", 1U) &&
           r_mir_write_type_text(context, symbol->return_type, r_llvm_key_write, writer) &&
           r_llvm_key_append(writer, "\x01", 1U) &&
           r_llvm_key_append(writer, display, strlen(display)) &&
           r_llvm_key_append(writer, "\x01", 1U) &&
           r_llvm_key_number(writer, symbol->name_span.start);
}

static int r_llvm_key_compare(const void *left_pointer, const void *right_pointer) {
    const RLlvmKeyText *left = left_pointer;
    const RLlvmKeyText *right = right_pointer;
    const size_t common = left->length < right->length ? left->length : right->length;
    const int comparison = common == 0U ? 0 : memcmp(left->bytes, right->bytes, common);

    if (comparison != 0) {
        return comparison;
    }
    if (left->length != right->length) {
        return left->length < right->length ? -1 : 1;
    }
    /* Equal texts name one thing twice; the id keeps the sort a total order. */
    return left->id < right->id ? -1 : (left->id != right->id ? 1 : 0);
}

typedef enum RLlvmKeyKind {
    R_LLVM_KEY_TYPE = 0,
    R_LLVM_KEY_SYMBOL,
    R_LLVM_KEY_SOURCE
} RLlvmKeyKind;

/* The ranks of `count` ids of one kind: keys[id] is the one-based position of the id's text. */
static uint32_t *r_llvm_rank(RLlvmEmitter *emitter, RLlvmKeyKind kind, size_t count) {
    RLlvmKeyText *texts = r_llvm_allocate(emitter, (count + 1U) * sizeof(*texts));
    uint32_t *keys = r_llvm_allocate(emitter, (count + 1U) * sizeof(*keys));
    bool success = (texts != NULL) && (keys != NULL);
    size_t index;

    for (index = 0U; success && (index < count); ++index) {
        RLlvmKeyWriter writer = {emitter, &texts[index]};
        texts[index].id = (uint32_t)(index + 1U);
        switch (kind) {
        case R_LLVM_KEY_TYPE:
            success = r_mir_write_type_text(
                emitter->frontend, (RTypeId)(index + 1U), r_llvm_key_write, &writer);
            break;
        case R_LLVM_KEY_SYMBOL:
            success = r_llvm_symbol_text(&writer, (RSymbolId)(index + 1U));
            break;
        default: {
            const char *module = r_llvm_key_module(emitter, (RSourceId)(index + 1U));
            success = r_llvm_key_append(&writer, module, strlen(module));
            break;
        }
        }
    }
    if (success) {
        qsort(texts, count, sizeof(*texts), r_llvm_key_compare);
        (void)memset(keys, 0, (count + 1U) * sizeof(*keys));
        for (index = 0U; index < count; ++index) {
            keys[texts[index].id] = (uint32_t)(index + 1U);
        }
    }
    for (index = 0U; (texts != NULL) && (index < count); ++index) {
        r_llvm_free(emitter, texts[index].bytes);
    }
    r_llvm_free(emitter, texts);
    if (!success) {
        r_llvm_free(emitter, keys);
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    return keys;
}

bool r_llvm_prepare_keys(RLlvmEmitter *emitter) {
    const RFrontendContext *context = emitter->frontend;

    emitter->type_keys = r_llvm_rank(emitter, R_LLVM_KEY_TYPE, context->semantic_type_count);
    emitter->symbol_keys = r_llvm_rank(emitter, R_LLVM_KEY_SYMBOL, context->semantic_symbol_count);
    emitter->source_keys = r_llvm_rank(emitter, R_LLVM_KEY_SOURCE, context->source_count);
    return (emitter->type_keys != NULL) && (emitter->symbol_keys != NULL) &&
           (emitter->source_keys != NULL);
}

void r_llvm_release_keys(RLlvmEmitter *emitter) {
    r_llvm_free(emitter, emitter->type_keys);
    r_llvm_free(emitter, emitter->symbol_keys);
    r_llvm_free(emitter, emitter->source_keys);
    emitter->type_keys = NULL;
    emitter->symbol_keys = NULL;
    emitter->source_keys = NULL;
}

uint32_t r_llvm_type_key(const RLlvmEmitter *emitter, RTypeId id) {
    return (id == R_TYPE_ID_INVALID) || ((size_t)id > emitter->frontend->semantic_type_count)
               ? 0U
               : emitter->type_keys[id];
}

uint32_t r_llvm_symbol_key(const RLlvmEmitter *emitter, RSymbolId id) {
    return (id == R_SYMBOL_ID_INVALID) || ((size_t)id > emitter->frontend->semantic_symbol_count)
               ? 0U
               : emitter->symbol_keys[id];
}

uint32_t r_llvm_source_key(const RLlvmEmitter *emitter, RSourceId id) {
    return (id == 0U) || ((size_t)id > emitter->frontend->source_count) ? 0U
                                                                        : emitter->source_keys[id];
}

/* Named metadata !r.sources of the IR: the module name of each source key, in the order of the
   keys, so a reader of the IR can tell the source of a panic's location (tests/llvm_checks.py). */
void r_llvm_source_report(RLlvmEmitter *emitter) {
    const size_t count = emitter->frontend->source_count;
    uint32_t key;

    for (key = 1U; key <= count; ++key) {
        size_t index;
        for (index = 1U; index <= count; ++index) {
            const char *module;
            LLVMMetadataRef fields[2];
            if (emitter->source_keys[index] != key) {
                continue;
            }
            module = r_llvm_key_module(emitter, (RSourceId)index);
            if (module[0] == '\0') {
                break;
            }
            fields[0] = LLVMValueAsMetadata(r_llvm_u32(emitter, key));
            fields[1] = LLVMMDStringInContext2(emitter->context, module, strlen(module));
            LLVMAddNamedMetadataOperand(
                emitter->module,
                "r.sources",
                LLVMMetadataAsValue(emitter->context,
                                    LLVMMDNodeInContext2(emitter->context, fields, 2U)));
            break;
        }
    }
}
