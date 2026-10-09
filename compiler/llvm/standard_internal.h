#ifndef R_LLVM_STANDARD_INTERNAL_H
#define R_LLVM_STANDARD_INTERNAL_H

#include "emit_internal.h"

/* Shared conventions of the standard operations (standard.c): operands, temporaries holding
   library structures, members by name, carriers and options built from library results. */

RMirValueId
r_llvm_std_operand(const RLlvmEmitter *emitter, const RMirInstruction *instruction, uint32_t index);
RTypeId r_llvm_std_operand_type(const RLlvmEmitter *emitter, RMirValueId value);
/* Zeroed memory of a runtime or library structure named by its typedef. */
LLVMValueRef r_llvm_std_structure(RLlvmEmitter *emitter, const char *type_name);
LLVMValueRef r_llvm_std_member(RLlvmEmitter *emitter,
                               LLVMValueRef memory,
                               const char *type_name,
                               const char *field_name);
/* Whether the 32-bit member holds the enumerator, as an i1. */
LLVMValueRef r_llvm_std_status_is(RLlvmEmitter *emitter,
                                  LLVMValueRef memory,
                                  const char *type_name,
                                  const char *field_name,
                                  const char *enumerator);
bool r_llvm_std_copy(RLlvmEmitter *emitter,
                     RTypeId type,
                     LLVMValueRef destination,
                     LLVMValueRef source);
/* A value staged in memory of its own for a call that may move it. */
LLVMValueRef r_llvm_std_stage(RLlvmEmitter *emitter, RMirValueId value, RTypeId type);
/* An integer operand as a C size_t. */
LLVMValueRef r_llvm_std_size(RLlvmEmitter *emitter, RMirValueId value);
LLVMValueRef r_llvm_std_allocator(RLlvmEmitter *emitter);
/* The zeroed memory of the instruction's result. */
LLVMValueRef r_llvm_std_result(RLlvmEmitter *emitter, const RMirInstruction *instruction);
void r_llvm_std_initialized(RLlvmEmitter *emitter, const RMirInstruction *instruction);
RTypeId r_llvm_std_single_effect(const RLlvmEmitter *emitter, RTypeId carrier);
/* A carrier result: tag 0 with the success member (none for void) when status holds, tag 1 with
   the error member otherwise. */
bool r_llvm_std_carrier(RLlvmEmitter *emitter,
                        const RMirInstruction *instruction,
                        LLVMValueRef status,
                        LLVMValueRef success_member,
                        LLVMValueRef error_member);
LLVMValueRef r_llvm_std_intrinsic(RLlvmEmitter *emitter,
                                  const char *name,
                                  LLVMTypeRef type,
                                  LLVMValueRef *arguments,
                                  unsigned count);
/* A library status other than `status` (or, with fails_when_equal, that status) ends the process
   as a contract violation. */
bool r_llvm_std_require_status(RLlvmEmitter *emitter,
                               const RMirInstruction *instruction,
                               LLVMValueRef native,
                               const char *type_name,
                               const char *status,
                               bool fails_when_equal);
bool r_llvm_std_option_from_native(RLlvmEmitter *emitter,
                                   const RMirInstruction *instruction,
                                   LLVMValueRef native,
                                   const char *type_name);
/* A library call that moves an optional value into the payload of the result; the last argument
   is set to that payload. */
bool r_llvm_std_value_option(RLlvmEmitter *emitter,
                             const RMirInstruction *instruction,
                             const char *function_name,
                             const char *type_name,
                             const char *success,
                             LLVMValueRef *arguments,
                             unsigned argument_count);
bool r_llvm_std_pointer_option(RLlvmEmitter *emitter,
                               const RMirInstruction *instruction,
                               const char *function_name,
                               const char *type_name,
                               const char *success,
                               LLVMValueRef *arguments,
                               unsigned argument_count);
bool r_llvm_std_alloc_carrier(RLlvmEmitter *emitter,
                              const RMirInstruction *instruction,
                              LLVMValueRef native,
                              const char *type_name,
                              const char *success,
                              const char *success_member);
/* An insertion of a staged value that returns it in push_error<T> on an allocation failure; the
   last argument is set to the staged value. */
bool r_llvm_std_staged_insert(RLlvmEmitter *emitter,
                              const RMirInstruction *instruction,
                              const char *function_name,
                              const char *type_name,
                              const char *success,
                              const char *contract,
                              LLVMValueRef *arguments,
                              unsigned argument_count,
                              RMirValueId value);

/* std.string, std.utf8, core::hash and core::key_equal (text.c). */
bool r_llvm_std_text(RLlvmEmitter *emitter, const RMirInstruction *instruction);
/* The atomic operations of core (atomics.c). */
bool r_llvm_std_atomic(RLlvmEmitter *emitter, const RMirInstruction *instruction);
/* Reflection of program types (reflection.c, R-REFL). */
bool r_llvm_std_reflection(RLlvmEmitter *emitter, const RMirInstruction *instruction);
/* Library operations that are one call of their C entry (library.c). */
bool r_llvm_std_library_handles(RStandardCallOperation operation);
bool r_llvm_std_library(RLlvmEmitter *emitter, const RMirInstruction *instruction);
/* std.array, std.list and std.dict (containers.c). */
bool r_llvm_std_container(RLlvmEmitter *emitter, const RMirInstruction *instruction);

#endif
