#include "emit_internal.h"

#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>

#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

/* DWARF of the program (B8.1). With RFrontendArtifactOptions.debug_info the module carries one
   compile unit, a file per R source, a subprogram per function of the program with its R name,
   the source line and column of every MIR instruction, and the parameters and locals of each
   function with their R types. The sources keep their R line structure: R-AGG-0012 code that
   @derive generates takes the position of its capability (r_frontend_source_position).

   DWARF has no language code for R; the unit says C11, so a debugger prints the values by their
   layout and evaluates expressions over them in C syntax. Functions the emitter writes without R
   source (glue, helpers, the C entry) have no subprogram, and instructions built for them while
   the builder was positioned in an R function lose that location when the module is finished.
   A type is described from the layouts the emitter has computed, never computed anew: a member
   or referent whose layout the program did not need is an opaque structure. */

static bool r_llvm_debug_enabled(const RLlvmEmitter *emitter) {
    return emitter->debug_builder != NULL;
}

/* The file of a source: an absolute name splits into its directory and base name, a relative
   one is relative to the working directory, as the compile unit's directory. */
static LLVMMetadataRef r_llvm_debug_file(RLlvmEmitter *emitter, RSourceId source) {
    const char *name;
    const char *slash;
    char directory[PATH_MAX];

    if ((source == R_SOURCE_ID_INVALID) ||
        ((size_t)source > r_frontend_source_count(emitter->frontend))) {
        return emitter->debug_files[0];
    }
    if (emitter->debug_files[source] != NULL) {
        return emitter->debug_files[source];
    }
    name = r_frontend_source_name(emitter->frontend, source);
    if (name == NULL) {
        name = "<source>";
    }
    slash = strrchr(name, '/');
    if ((name[0] == '/') && (slash != NULL)) {
        emitter->debug_files[source] =
            LLVMDIBuilderCreateFile(emitter->debug_builder,
                                    slash + 1,
                                    strlen(slash + 1),
                                    name,
                                    (size_t)(slash - name) == 0U ? 1U : (size_t)(slash - name));
    } else {
        if (getcwd(directory, sizeof(directory)) == NULL) {
            directory[0] = '.';
            directory[1] = '\0';
        }
        emitter->debug_files[source] = LLVMDIBuilderCreateFile(
            emitter->debug_builder, name, strlen(name), directory, strlen(directory));
    }
    return emitter->debug_files[source];
}

bool r_llvm_debug_begin(RLlvmEmitter *emitter, RSourceId unit_source) {
    const RFrontendArtifactOptions *options = emitter->artifact_options;
    const size_t source_count = r_frontend_source_count(emitter->frontend);
    const size_t type_count = emitter->frontend->semantic_type_count;
    LLVMMetadataRef file;
    static const char producer[] = "r-front";

    if ((options == NULL) || (!options->debug_info && (options->coverage_prefix == NULL))) {
        return true;
    }
    emitter->debug_files = r_llvm_allocate(emitter, (source_count + 1U) * sizeof(LLVMMetadataRef));
    emitter->debug_types = r_llvm_allocate(emitter, (type_count + 1U) * sizeof(LLVMMetadataRef));
    if ((emitter->debug_files == NULL) || (emitter->debug_types == NULL)) {
        return false;
    }
    emitter->debug_builder = LLVMCreateDIBuilder(emitter->module);
    file = r_llvm_debug_file(emitter, unit_source);
    if (file == NULL) {
        file = LLVMDIBuilderCreateFile(emitter->debug_builder, "<program>", 9U, ".", 1U);
    }
    emitter->debug_files[0] = file;
    emitter->debug_unit = LLVMDIBuilderCreateCompileUnit(emitter->debug_builder,
                                                         LLVMDWARFSourceLanguageC11,
                                                         file,
                                                         producer,
                                                         sizeof(producer) - 1U,
                                                         options->optimization_level != 0U,
                                                         "",
                                                         0U,
                                                         0U,
                                                         "",
                                                         0U,
                                                         LLVMDWARFEmissionFull,
                                                         0U,
                                                         0,
                                                         0,
                                                         "",
                                                         0U,
                                                         "",
                                                         0U);
    LLVMAddModuleFlag(emitter->module,
                      LLVMModuleFlagBehaviorWarning,
                      "Debug Info Version",
                      18U,
                      LLVMValueAsMetadata(r_llvm_u32(emitter, LLVMDebugMetadataVersion())));
    LLVMAddModuleFlag(emitter->module,
                      LLVMModuleFlagBehaviorWarning,
                      "Dwarf Version",
                      13U,
                      LLVMValueAsMetadata(r_llvm_u32(emitter, 5U)));
    return true;
}

/* Instructions of functions without a subprogram carry no location (see the file comment). */
static void r_llvm_debug_strip_unscoped(RLlvmEmitter *emitter) {
    LLVMValueRef function;

    for (function = LLVMGetFirstFunction(emitter->module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        LLVMBasicBlockRef block;
        if (LLVMGetSubprogram(function) != NULL) {
            continue;
        }
        for (block = LLVMGetFirstBasicBlock(function); block != NULL;
             block = LLVMGetNextBasicBlock(block)) {
            LLVMValueRef instruction;
            for (instruction = LLVMGetFirstInstruction(block); instruction != NULL;
                 instruction = LLVMGetNextInstruction(instruction)) {
                if (LLVMInstructionGetDebugLoc(instruction) != NULL) {
                    LLVMInstructionSetDebugLoc(instruction, NULL);
                }
            }
        }
    }
}

void r_llvm_debug_finish(RLlvmEmitter *emitter) {
    if (!r_llvm_debug_enabled(emitter)) {
        return;
    }
    LLVMSetCurrentDebugLocation2(emitter->builder, NULL);
    r_llvm_debug_strip_unscoped(emitter);
    LLVMDIBuilderFinalize(emitter->debug_builder);
}

void r_llvm_debug_release(RLlvmEmitter *emitter) {
    if (emitter->debug_builder != NULL) {
        LLVMDisposeDIBuilder(emitter->debug_builder);
        emitter->debug_builder = NULL;
    }
    r_llvm_free(emitter, emitter->debug_files);
    r_llvm_free(emitter, emitter->debug_types);
    emitter->debug_files = NULL;
    emitter->debug_types = NULL;
}

/* ---- Types ---- */

/* The layout the emitter computed for a type, or false when it has none. */
static bool
r_llvm_debug_layout(RLlvmEmitter *emitter, RTypeId id, uint32_t *size, uint32_t *align) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);

    if ((value_id == R_TYPE_ID_INVALID) ||
        ((size_t)value_id > emitter->frontend->semantic_type_count) || (emitter->layouts == NULL) ||
        (emitter->layouts[value_id].state != 2U)) {
        return false;
    }
    *size = emitter->layouts[value_id].size;
    *align = emitter->layouts[value_id].align;
    return true;
}

static const char *r_llvm_debug_text(RLlvmEmitter *emitter, uint32_t intern, size_t *length) {
    const RInternEntry *entry;

    if ((intern == 0U) || ((size_t)intern > emitter->frontend->intern_count)) {
        *length = 0U;
        return "";
    }
    entry = &emitter->frontend->intern_entries[(size_t)intern - 1U];
    *length = entry->length;
    return (const char *)entry->bytes;
}

/* The R spelling of a type, as far as the buffer holds it. */
static void r_llvm_debug_type_name(
    RLlvmEmitter *emitter, RTypeId id, char *buffer, size_t capacity, uint32_t depth) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    char inner[96];
    char second[96];

    buffer[0] = '\0';
    if ((type == NULL) || (depth > 4U)) {
        (void)snprintf(buffer, capacity, "?");
        return;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, id);
        const RSource *source =
            aggregate == NULL ? NULL
                              : r_get_source_const(emitter->frontend, aggregate->module_source);
        size_t length = 0U;
        const char *name =
            aggregate == NULL ? "" : r_llvm_debug_text(emitter, aggregate->name_intern_id, &length);
        (void)snprintf(buffer,
                       capacity,
                       "%s%s%.*s",
                       (source != NULL) && (source->module_name != NULL) ? source->module_name : "",
                       (source != NULL) && (source->module_name != NULL) ? "::" : "",
                       (int)length,
                       name);
        return;
    }
    case R_SEMANTIC_TYPE_BORROW:
    case R_SEMANTIC_TYPE_RAW:
        r_llvm_debug_type_name(emitter, type->base, inner, sizeof(inner), depth + 1U);
        (void)snprintf(
            buffer, capacity, "%s%s*", type->kind == R_SEMANTIC_TYPE_RAW ? "raw " : "", inner);
        return;
    case R_SEMANTIC_TYPE_SLICE:
        r_llvm_debug_type_name(emitter, type->base, inner, sizeof(inner), depth + 1U);
        (void)snprintf(buffer, capacity, "%s[]", inner);
        return;
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        r_llvm_debug_type_name(emitter, type->base, inner, sizeof(inner), depth + 1U);
        (void)snprintf(buffer, capacity, "%s[%llu]", inner, (unsigned long long)type->length);
        return;
    case R_SEMANTIC_TYPE_ARRAY:
    case R_SEMANTIC_TYPE_LIST:
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_TASK:
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_WEAK:
    case R_SEMANTIC_TYPE_OWN: {
        const char *constructor = type->kind == R_SEMANTIC_TYPE_ARRAY    ? "array"
                                  : type->kind == R_SEMANTIC_TYPE_LIST   ? "list"
                                  : type->kind == R_SEMANTIC_TYPE_OPTION ? "o"
                                  : type->kind == R_SEMANTIC_TYPE_TASK   ? "task"
                                  : type->kind == R_SEMANTIC_TYPE_ARC    ? "arc"
                                  : type->kind == R_SEMANTIC_TYPE_RC     ? "rc"
                                  : type->kind == R_SEMANTIC_TYPE_WEAK   ? "weak"
                                                                         : "own";
        r_llvm_debug_type_name(emitter, type->base, inner, sizeof(inner), depth + 1U);
        (void)snprintf(buffer, capacity, "%s<%s>", constructor, inner);
        return;
    }
    case R_SEMANTIC_TYPE_DICT:
    case R_SEMANTIC_TYPE_RESULT:
        r_llvm_debug_type_name(emitter, type->base, inner, sizeof(inner), depth + 1U);
        r_llvm_debug_type_name(emitter, type->second, second, sizeof(second), depth + 1U);
        (void)snprintf(buffer,
                       capacity,
                       "%s<%s, %s>",
                       type->kind == R_SEMANTIC_TYPE_DICT ? "dict" : "result",
                       inner,
                       second);
        return;
    case R_SEMANTIC_TYPE_STANDARD: {
        const RInternEntry *entry = r_llvm_standard_name(emitter, type);
        const uint32_t arity = r_llvm_standard_arity(type);
        if (entry == NULL) {
            (void)snprintf(buffer, capacity, "standard");
            return;
        }
        if (arity != 0U) {
            r_llvm_debug_type_name(emitter, type->base, inner, sizeof(inner), depth + 1U);
        }
        if (arity == 2U) {
            r_llvm_debug_type_name(emitter, type->second, second, sizeof(second), depth + 1U);
        }
        (void)snprintf(buffer,
                       capacity,
                       "%.*s%s%s%s%s%s",
                       (int)entry->length,
                       (const char *)entry->bytes,
                       arity != 0U ? "<" : "",
                       arity != 0U ? inner : "",
                       arity == 2U ? ", " : "",
                       arity == 2U ? second : "",
                       arity != 0U ? ">" : "");
        return;
    }
    default: {
        const char *kind = r_semantic_type_kind_name(type->kind);
        (void)snprintf(buffer, capacity, "%s", kind == NULL ? "?" : kind);
        return;
    }
    }
}

static LLVMMetadataRef
r_llvm_debug_basic(RLlvmEmitter *emitter, const char *name, uint64_t bits, unsigned encoding) {
    return LLVMDIBuilderCreateBasicType(
        emitter->debug_builder, name, strlen(name), bits, encoding, LLVMDIFlagZero);
}

static LLVMMetadataRef
r_llvm_debug_opaque(RLlvmEmitter *emitter, const char *name, uint32_t size, uint32_t align) {
    return LLVMDIBuilderCreateStructType(emitter->debug_builder,
                                         emitter->debug_unit,
                                         name,
                                         strlen(name),
                                         emitter->debug_files[0],
                                         0U,
                                         (uint64_t)size * 8U,
                                         align * 8U,
                                         LLVMDIFlagZero,
                                         NULL,
                                         NULL,
                                         0U,
                                         0U,
                                         NULL,
                                         "",
                                         0U);
}

static LLVMMetadataRef r_llvm_debug_member(RLlvmEmitter *emitter,
                                           LLVMMetadataRef scope,
                                           const char *name,
                                           size_t length,
                                           uint64_t offset,
                                           LLVMMetadataRef type,
                                           uint64_t bits,
                                           uint32_t align) {
    return LLVMDIBuilderCreateMemberType(emitter->debug_builder,
                                         scope,
                                         name,
                                         length,
                                         emitter->debug_files[0],
                                         0U,
                                         bits,
                                         align * 8U,
                                         offset * 8U,
                                         LLVMDIFlagZero,
                                         type);
}

static LLVMMetadataRef r_llvm_debug_type_at(RLlvmEmitter *emitter, RTypeId id, uint32_t depth);

/* A struct of the program with its fields, through a replaceable placeholder so that a field
   that refers back to the struct finds it. */
static LLVMMetadataRef r_llvm_debug_struct(RLlvmEmitter *emitter,
                                           RTypeId value_id,
                                           const RSemanticAggregate *aggregate,
                                           const char *name,
                                           uint32_t size,
                                           uint32_t align,
                                           uint32_t depth) {
    LLVMMetadataRef placeholder;
    LLVMMetadataRef result;
    LLVMMetadataRef members[64];
    uint32_t count = 0U;
    uint32_t index;

    placeholder = LLVMDIBuilderCreateReplaceableCompositeType(emitter->debug_builder,
                                                              aggregate->is_union ? 0x17U : 0x13U,
                                                              name,
                                                              strlen(name),
                                                              emitter->debug_unit,
                                                              emitter->debug_files[0],
                                                              0U,
                                                              0U,
                                                              (uint64_t)size * 8U,
                                                              align * 8U,
                                                              LLVMDIFlagZero,
                                                              "",
                                                              0U);
    emitter->debug_types[value_id] = placeholder;
    for (index = 0U; (index < aggregate->field_count) && (count < 64U); ++index) {
        const uint32_t field_id = aggregate->first_field + index + 1U;
        const RSemanticField *field = r_llvm_field(emitter, field_id);
        uint32_t field_size = 0U;
        uint32_t field_align = 0U;
        uint32_t offset = 0U;
        size_t length = 0U;
        const char *field_name;
        const RFrontendStatus status = emitter->status;
        if ((field == NULL) ||
            !r_llvm_debug_layout(emitter, field->type, &field_size, &field_align)) {
            continue;
        }
        if (!r_llvm_field_offset(emitter, field_id, &offset)) {
            emitter->status = status;
            continue;
        }
        field_name = r_llvm_debug_text(emitter, field->name_intern_id, &length);
        if (field->bit_width != 0U) {
            /* R-FFI-0062: a bit-field member at its bit offset in the struct. */
            RSemanticFieldPlace place;
            if (!r_llvm_field_place(emitter, field_id, &place)) {
                emitter->status = status;
                continue;
            }
            members[count++] = LLVMDIBuilderCreateBitFieldMemberType(
                emitter->debug_builder,
                placeholder,
                field_name,
                length,
                emitter->debug_files[0],
                0U,
                place.bit_width,
                (place.offset * 8U) + place.bit_offset,
                0U,
                LLVMDIFlagZero,
                r_llvm_debug_type_at(emitter, field->type, depth + 1U));
            continue;
        }
        members[count++] =
            r_llvm_debug_member(emitter,
                                placeholder,
                                field_name,
                                length,
                                offset,
                                r_llvm_debug_type_at(emitter, field->type, depth + 1U),
                                (uint64_t)field_size * 8U,
                                field_align);
    }
    /* R-AGG-0014: a union is described as one, every member at offset zero. */
    result = aggregate->is_union ? LLVMDIBuilderCreateUnionType(emitter->debug_builder,
                                                                emitter->debug_unit,
                                                                name,
                                                                strlen(name),
                                                                emitter->debug_files[0],
                                                                0U,
                                                                (uint64_t)size * 8U,
                                                                align * 8U,
                                                                LLVMDIFlagZero,
                                                                members,
                                                                count,
                                                                0U,
                                                                "",
                                                                0U)
                                 : LLVMDIBuilderCreateStructType(emitter->debug_builder,
                                                                 emitter->debug_unit,
                                                                 name,
                                                                 strlen(name),
                                                                 emitter->debug_files[0],
                                                                 0U,
                                                                 (uint64_t)size * 8U,
                                                                 align * 8U,
                                                                 LLVMDIFlagZero,
                                                                 NULL,
                                                                 members,
                                                                 count,
                                                                 0U,
                                                                 NULL,
                                                                 "",
                                                                 0U);
    LLVMMetadataReplaceAllUsesWith(placeholder, result);
    emitter->debug_types[value_id] = result;
    return result;
}

/* A view {data, length}: a slice or a string view. */
static LLVMMetadataRef
r_llvm_debug_view(RLlvmEmitter *emitter, const char *name, LLVMMetadataRef element) {
    LLVMMetadataRef members[2];
    LLVMMetadataRef data =
        LLVMDIBuilderCreatePointerType(emitter->debug_builder, element, 64U, 64U, 0U, "", 0U);
    LLVMMetadataRef length = r_llvm_debug_basic(emitter, "usize", 64U, 0x07U);

    members[0] = r_llvm_debug_member(emitter, emitter->debug_unit, "data", 4U, 0U, data, 64U, 8U);
    members[1] =
        r_llvm_debug_member(emitter, emitter->debug_unit, "length", 6U, 8U, length, 64U, 8U);
    return LLVMDIBuilderCreateStructType(emitter->debug_builder,
                                         emitter->debug_unit,
                                         name,
                                         strlen(name),
                                         emitter->debug_files[0],
                                         0U,
                                         128U,
                                         64U,
                                         LLVMDIFlagZero,
                                         NULL,
                                         members,
                                         2U,
                                         0U,
                                         NULL,
                                         "",
                                         0U);
}

/* An owned string: its bytes, the data and length of the runtime array they live in. */
static LLVMMetadataRef
r_llvm_debug_string(RLlvmEmitter *emitter, const char *name, uint32_t size, uint32_t align) {
    uint32_t bytes = 0U;
    uint32_t data = 0U;
    uint32_t length = 8U;
    LLVMMetadataRef members[2];

    if (!r_llvm_runtime_field(emitter, "RRuntimeString", "bytes", &bytes, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeArray", "data", &data, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeArray", "length", &length, NULL)) {
        return r_llvm_debug_opaque(emitter, name, size, align);
    }
    members[0] = r_llvm_debug_member(
        emitter,
        emitter->debug_unit,
        "data",
        4U,
        (uint64_t)bytes + data,
        LLVMDIBuilderCreatePointerType(emitter->debug_builder,
                                       r_llvm_debug_basic(emitter, "u8", 8U, 0x08U),
                                       64U,
                                       64U,
                                       0U,
                                       "",
                                       0U),
        64U,
        8U);
    members[1] = r_llvm_debug_member(emitter,
                                     emitter->debug_unit,
                                     "length",
                                     6U,
                                     (uint64_t)bytes + length,
                                     r_llvm_debug_basic(emitter, "usize", 64U, 0x07U),
                                     64U,
                                     8U);
    return LLVMDIBuilderCreateStructType(emitter->debug_builder,
                                         emitter->debug_unit,
                                         name,
                                         strlen(name),
                                         emitter->debug_files[0],
                                         0U,
                                         (uint64_t)size * 8U,
                                         align * 8U,
                                         LLVMDIFlagZero,
                                         NULL,
                                         members,
                                         2U,
                                         0U,
                                         NULL,
                                         "",
                                         0U);
}

static LLVMMetadataRef r_llvm_debug_type_at(RLlvmEmitter *emitter, RTypeId id, uint32_t depth) {
    const RTypeId value_id = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value_id);
    uint32_t size = 0U;
    uint32_t align = 1U;
    bool laid_out;
    char name[160];
    LLVMMetadataRef result = NULL;

    if ((type == NULL) || (value_id == R_TYPE_ID_INVALID) ||
        ((size_t)value_id > emitter->frontend->semantic_type_count)) {
        return r_llvm_debug_opaque(emitter, "?", 0U, 1U);
    }
    if (emitter->debug_types[value_id] != NULL) {
        return emitter->debug_types[value_id];
    }
    r_llvm_debug_type_name(emitter, id, name, sizeof(name), 0U);
    laid_out = r_llvm_debug_layout(emitter, id, &size, &align);
    if (depth > 8U) {
        return r_llvm_debug_opaque(emitter, name, size, align);
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_BOOL:
        result = r_llvm_debug_basic(emitter, "bool", 8U, 0x02U);
        break;
    case R_SEMANTIC_TYPE_I8:
    case R_SEMANTIC_TYPE_I16:
    case R_SEMANTIC_TYPE_I32:
    case R_SEMANTIC_TYPE_I64:
    case R_SEMANTIC_TYPE_ISIZE:
        result = r_llvm_debug_basic(emitter, name, (uint64_t)size * 8U, 0x05U);
        break;
    case R_SEMANTIC_TYPE_U8:
    case R_SEMANTIC_TYPE_U16:
    case R_SEMANTIC_TYPE_U32:
    case R_SEMANTIC_TYPE_U64:
    case R_SEMANTIC_TYPE_USIZE:
        result = r_llvm_debug_basic(emitter, name, (uint64_t)size * 8U, 0x07U);
        break;
    case R_SEMANTIC_TYPE_F32:
    case R_SEMANTIC_TYPE_F64:
        result =
            r_llvm_debug_basic(emitter, name, type->kind == R_SEMANTIC_TYPE_F32 ? 32U : 64U, 0x04U);
        break;
    case R_SEMANTIC_TYPE_CHAR:
        result = r_llvm_debug_basic(emitter, "char", 32U, 0x10U);
        break;
    case R_SEMANTIC_TYPE_STR:
    case R_SEMANTIC_TYPE_CONSTEXPR_STR:
        result = r_llvm_debug_view(emitter, "str", r_llvm_debug_basic(emitter, "u8", 8U, 0x08U));
        break;
    case R_SEMANTIC_TYPE_SLICE:
        result =
            r_llvm_debug_view(emitter, name, r_llvm_debug_type_at(emitter, type->base, depth + 1U));
        break;
    case R_SEMANTIC_TYPE_BORROW:
    case R_SEMANTIC_TYPE_RAW: {
        uint32_t referent_size = 0U;
        uint32_t referent_align = 0U;
        const RSemanticType *referent =
            r_llvm_type(emitter, r_llvm_value_type(emitter, type->base));
        /* A referent without a layout (an interface, an unsized view) is shown as its name. */
        LLVMMetadataRef pointee =
            (referent != NULL) &&
                    r_llvm_debug_layout(emitter, type->base, &referent_size, &referent_align)
                ? r_llvm_debug_type_at(emitter, type->base, depth + 1U)
                : NULL;
        result = LLVMDIBuilderCreatePointerType(
            emitter->debug_builder, pointee, 64U, 64U, 0U, name, strlen(name));
        break;
    }
    case R_SEMANTIC_TYPE_FIXED_ARRAY: {
        uint32_t element_size = 0U;
        uint32_t element_align = 0U;
        LLVMMetadataRef subrange;
        if (!laid_out || !r_llvm_debug_layout(emitter, type->base, &element_size, &element_align)) {
            result = r_llvm_debug_opaque(emitter, name, size, align);
            break;
        }
        subrange =
            LLVMDIBuilderGetOrCreateSubrange(emitter->debug_builder, 0, (int64_t)type->length);
        result = LLVMDIBuilderCreateArrayType(emitter->debug_builder,
                                              (uint64_t)size * 8U,
                                              align * 8U,
                                              r_llvm_debug_type_at(emitter, type->base, depth + 1U),
                                              &subrange,
                                              1U);
        break;
    }
    case R_SEMANTIC_TYPE_STANDARD:
        if (laid_out && r_llvm_standard_named(emitter, value_id, "std.string::string")) {
            result = r_llvm_debug_string(emitter, name, size, align);
        } else {
            result = r_llvm_debug_opaque(emitter, name, size, align);
        }
        break;
    case R_SEMANTIC_TYPE_STRUCT: {
        const RSemanticAggregate *aggregate = r_llvm_aggregate(emitter, value_id);
        if ((aggregate == NULL) || aggregate->is_tagged || !laid_out) {
            result = r_llvm_debug_opaque(emitter, name, size, align);
            break;
        }
        return r_llvm_debug_struct(emitter, value_id, aggregate, name, size, align, depth);
    }
    default:
        result = r_llvm_debug_opaque(emitter, name, size, align);
        break;
    }
    emitter->debug_types[value_id] = result;
    return result;
}

/* ---- Functions, locations and variables ---- */

static LLVMMetadataRef r_llvm_debug_scope_for(RLlvmEmitter *emitter, RSourceId source) {
    if ((source == R_SOURCE_ID_INVALID) || (source == emitter->debug_source)) {
        return emitter->debug_scope;
    }
    /* Code of another source in this function (a field initializer of a struct declared there):
       its lines belong to that file. */
    return LLVMDIBuilderCreateLexicalBlockFile(
        emitter->debug_builder, emitter->debug_scope, r_llvm_debug_file(emitter, source), 0U);
}

void r_llvm_debug_function(RLlvmEmitter *emitter,
                           LLVMValueRef function,
                           const RSemanticSymbol *symbol) {
    const RFrontendArtifactOptions *options = emitter->artifact_options;
    RSourceSpan span = symbol->name_span;
    uint32_t line = 0U;
    uint32_t column = 0U;
    size_t name_length = 0U;
    size_t linkage_length = 0U;
    const char *name;
    LLVMMetadataRef file;
    LLVMMetadataRef subroutine;

    emitter->debug_scope = NULL;
    if (!r_llvm_debug_enabled(emitter)) {
        return;
    }
    r_frontend_source_position(emitter->frontend, span, &line, &column);
    file = r_llvm_debug_file(emitter, span.source);
    name = LLVMGetValueName2(function, &name_length);
    linkage_length = name_length;
    subroutine =
        LLVMDIBuilderCreateSubroutineType(emitter->debug_builder, file, NULL, 0U, LLVMDIFlagZero);
    emitter->debug_scope =
        LLVMDIBuilderCreateFunction(emitter->debug_builder,
                                    file,
                                    name,
                                    name_length,
                                    name,
                                    linkage_length,
                                    file,
                                    line,
                                    subroutine,
                                    LLVMGetLinkage(function) == LLVMInternalLinkage,
                                    1,
                                    line,
                                    LLVMDIFlagZero,
                                    (options != NULL) && (options->optimization_level != 0U));
    emitter->debug_source = span.source;
    LLVMSetSubprogram(function, emitter->debug_scope);
    LLVMSetCurrentDebugLocation2(emitter->builder,
                                 LLVMDIBuilderCreateDebugLocation(
                                     emitter->context, line, column, emitter->debug_scope, NULL));
}

void r_llvm_debug_location(RLlvmEmitter *emitter, RSourceSpan span) {
    uint32_t line = 0U;
    uint32_t column = 0U;

    if ((emitter->debug_scope == NULL) || (span.source == R_SOURCE_ID_INVALID)) {
        return;
    }
    r_frontend_source_position(emitter->frontend, span, &line, &column);
    if (line == 0U) {
        return;
    }
    LLVMSetCurrentDebugLocation2(
        emitter->builder,
        LLVMDIBuilderCreateDebugLocation(
            emitter->context, line, column, r_llvm_debug_scope_for(emitter, span.source), NULL));
}

void r_llvm_debug_leave(RLlvmEmitter *emitter) {
    if (emitter->debug_scope == NULL) {
        return;
    }
    LLVMSetCurrentDebugLocation2(emitter->builder, NULL);
    LLVMDIBuilderFinalizeSubprogram(emitter->debug_builder, emitter->debug_scope);
    emitter->debug_scope = NULL;
    emitter->debug_source = R_SOURCE_ID_INVALID;
}

/* The storage of a variable as a base and a byte offset: a local the emitter places inside other
   storage (the payload of a carrier, a slot of an async frame) is a chain of byte GEPs, which
   instruction selection does not follow; the declaration names the base and the offset. */
static LLVMValueRef r_llvm_debug_base(LLVMValueRef storage, uint64_t *offset) {
    *offset = 0U;
    while ((LLVMIsAGetElementPtrInst(storage) != NULL) && (LLVMGetNumOperands(storage) == 2) &&
           (LLVMIsAConstantInt(LLVMGetOperand(storage, 1U)) != NULL) &&
           (LLVMGetGEPSourceElementType(storage) ==
            LLVMInt8TypeInContext(LLVMGetTypeContext(LLVMTypeOf(storage))))) {
        *offset += LLVMConstIntGetZExtValue(LLVMGetOperand(storage, 1U));
        storage = LLVMGetOperand(storage, 0U);
    }
    return storage;
}

/* The parameters and locals of the function, declared at the storage the emitter gave them
   (stack slots, frame slots of an async step, or the memory of a parameter passed in memory). */
void r_llvm_debug_variables(RLlvmEmitter *emitter) {
    uint32_t block_index;
    LLVMBasicBlockRef entry;
    bool *declared;

    if (emitter->debug_scope == NULL) {
        return;
    }
    declared = r_llvm_allocate(emitter, emitter->parameter_count + emitter->local_count + 1U);
    if (declared == NULL) {
        return;
    }
    entry = LLVMGetInsertBlock(emitter->builder);
    for (block_index = 0U; block_index < emitter->mir->block_count; ++block_index) {
        const RMirBlock *block =
            &emitter->frontend->mir_blocks[(size_t)emitter->mir->first_block + block_index];
        uint32_t index;
        for (index = 0U; index < block->instruction_count; ++index) {
            const RMirInstruction *instruction =
                &emitter->frontend->mir_instructions[(size_t)block->first_instruction + index];
            const bool parameter = instruction->kind == R_MIR_INSTRUCTION_PARAMETER;
            const RSemanticSymbol *symbol;
            LLVMValueRef storage;
            LLVMMetadataRef variable;
            LLVMMetadataRef location;
            uint32_t line = 0U;
            uint32_t column = 0U;
            size_t length = 0U;
            const char *name;
            if (!parameter && (instruction->kind != R_MIR_INSTRUCTION_LOCAL)) {
                continue;
            }
            if ((size_t)instruction->place_ordinal >=
                (parameter ? emitter->parameter_count : emitter->local_count)) {
                continue;
            }
            if (declared[parameter ? instruction->place_ordinal
                                   : emitter->parameter_count + instruction->place_ordinal]) {
                continue;
            }
            declared[parameter ? instruction->place_ordinal
                               : emitter->parameter_count + instruction->place_ordinal] = true;
            storage = parameter ? emitter->parameters[instruction->place_ordinal]
                                : emitter->locals[instruction->place_ordinal];
            symbol = ((instruction->symbol == 0U) ||
                      ((size_t)instruction->symbol > emitter->frontend->semantic_symbol_count))
                         ? NULL
                         : &emitter->frontend->semantic_symbols[(size_t)instruction->symbol - 1U];
            if ((storage == NULL) || (symbol == NULL) || symbol->is_static || symbol->is_import ||
                (LLVMGetTypeKind(LLVMTypeOf(storage)) != LLVMPointerTypeKind) ||
                (LLVMIsAGlobalValue(storage) != NULL)) {
                continue;
            }
            name = r_llvm_debug_text(emitter, symbol->name_intern_id, &length);
            if ((length == 0U) || (name[0] == '$')) {
                continue;
            }
            r_frontend_source_position(emitter->frontend, symbol->name_span, &line, &column);
            variable = parameter ? LLVMDIBuilderCreateParameterVariable(
                                       emitter->debug_builder,
                                       emitter->debug_scope,
                                       name,
                                       length,
                                       instruction->place_ordinal + 1U,
                                       r_llvm_debug_file(emitter, emitter->debug_source),
                                       line,
                                       r_llvm_debug_type_at(emitter, instruction->type, 0U),
                                       1,
                                       LLVMDIFlagZero)
                                 : LLVMDIBuilderCreateAutoVariable(
                                       emitter->debug_builder,
                                       emitter->debug_scope,
                                       name,
                                       length,
                                       r_llvm_debug_file(emitter, emitter->debug_source),
                                       line,
                                       r_llvm_debug_type_at(emitter, instruction->type, 0U),
                                       1,
                                       LLVMDIFlagZero,
                                       0U);
            location = LLVMDIBuilderCreateDebugLocation(
                emitter->context, line, column, emitter->debug_scope, NULL);
            {
                uint64_t offset = 0U;
                uint64_t operations[2];
                LLVMValueRef base = r_llvm_debug_base(storage, &offset);
                operations[0] = 0x23U; /* DW_OP_plus_uconst */
                operations[1] = offset;
                (void)LLVMDIBuilderInsertDeclareRecordAtEnd(
                    emitter->debug_builder,
                    base,
                    variable,
                    LLVMDIBuilderCreateExpression(emitter->debug_builder,
                                                  offset == 0U ? NULL : operations,
                                                  offset == 0U ? 0U : 2U),
                    location,
                    entry);
            }
        }
    }
    r_llvm_free(emitter, declared);
}
