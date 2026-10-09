#ifndef R_LLVM_JSON_INTERNAL_H
#define R_LLVM_JSON_INTERNAL_H

#include "standard_internal.h"

/* Shared builders of the JSON decoders (json_decode.c) and stream decoders (json_stream.c). */

typedef struct RLlvmJdField {
    const RSemanticField *field;
    uint32_t index; /* in `present` and the switch */
} RLlvmJdField;

typedef struct RLlvmJdSlots {
    LLVMValueRef *addresses;
    const RJsonSchemaField *entries;
} RLlvmJdSlots;

LLVMBasicBlockRef r_llvm_jd_block(RLlvmEmitter *emitter);
LLVMValueRef r_llvm_jd_true(RLlvmEmitter *emitter);
LLVMValueRef r_llvm_jd_false(RLlvmEmitter *emitter);
LLVMValueRef r_llvm_jd_truth(RLlvmEmitter *emitter, LLVMValueRef value);
LLVMValueRef
r_llvm_jd_call(RLlvmEmitter *emitter, const char *name, LLVMValueRef *arguments, unsigned count);
bool r_llvm_jd_constant(RLlvmEmitter *emitter, const char *name, uint64_t *value);
LLVMValueRef r_llvm_jd_token(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *kind);
LLVMValueRef r_llvm_jd_next(RLlvmEmitter *emitter, LLVMValueRef cursor);
LLVMValueRef r_llvm_jd_fail(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *code);
LLVMValueRef
r_llvm_jd_has(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *kind, bool equal);
LLVMValueRef r_llvm_jd_text(RLlvmEmitter *emitter, LLVMValueRef cursor);
LLVMValueRef r_llvm_jd_options(RLlvmEmitter *emitter, LLVMValueRef cursor, const char *member);
LLVMValueRef r_llvm_jd_allocator(RLlvmEmitter *emitter, LLVMValueRef cursor);
void r_llvm_jd_require(RLlvmEmitter *emitter, LLVMValueRef condition, LLVMBasicBlockRef failure);
void r_llvm_jd_return_if(RLlvmEmitter *emitter, LLVMValueRef condition, LLVMValueRef value);
LLVMValueRef r_llvm_jd_decode(RLlvmEmitter *emitter,
                              RTypeId type,
                              LLVMValueRef cursor,
                              LLVMValueRef out,
                              LLVMValueRef quoted,
                              LLVMValueRef defaulted);
bool r_llvm_jd_ordinary_call(RLlvmEmitter *emitter,
                             RTypeId type,
                             LLVMValueRef cursor,
                             LLVMValueRef out);
LLVMValueRef r_llvm_jd_bool(RLlvmEmitter *emitter, bool value);
bool r_llvm_jd_call_function(RLlvmEmitter *emitter,
                             RSymbolId id,
                             RTypeId type,
                             LLVMValueRef argument,
                             LLVMValueRef cursor,
                             LLVMValueRef out,
                             LLVMBasicBlockRef failure);
bool r_llvm_jd_select(RLlvmEmitter *emitter,
                      const RLlvmJdField *fields,
                      uint32_t count,
                      LLVMValueRef cursor,
                      LLVMValueRef key,
                      LLVMValueRef selected,
                      LLVMBasicBlockRef failure);
LLVMValueRef r_llvm_jd_present(RLlvmEmitter *emitter, LLVMValueRef present, uint32_t index);
LLVMValueRef r_llvm_jd_is_present(RLlvmEmitter *emitter, LLVMValueRef present, uint32_t index);
void r_llvm_jd_set_present(RLlvmEmitter *emitter, LLVMValueRef present, uint32_t index);
bool r_llvm_jd_refusals(RLlvmEmitter *emitter,
                        LLVMValueRef cursor,
                        LLVMValueRef selected,
                        LLVMValueRef present,
                        LLVMBasicBlockRef failure);
bool r_llvm_jd_collector(RLlvmEmitter *emitter,
                         const RSemanticField *field,
                         LLVMValueRef slot,
                         LLVMValueRef present_flag,
                         LLVMValueRef cursor,
                         LLVMBasicBlockRef failure);
bool r_llvm_jd_embedded_defaults(RLlvmEmitter *emitter,
                                 const RSemanticAggregate *aggregate,
                                 const RLlvmJdSlots *slots,
                                 LLVMValueRef present,
                                 LLVMValueRef cursor,
                                 uint32_t parent,
                                 uint32_t first,
                                 uint32_t end,
                                 LLVMBasicBlockRef failure);

#endif
