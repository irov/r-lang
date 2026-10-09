#ifndef R_LLVM_EMIT_INTERNAL_H
#define R_LLVM_EMIT_INTERNAL_H

#include "abi.h"
#include "frontend_internal.h"
#include "surface.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The LLVM emitter (B4 of the LLVM transition): MIR of a whole program to an LLVM module for
   the manifest target. Generated code keeps the representation and the C calling convention of
   the C17 emitter, so the runtime and library C see the same values. */

typedef struct RLlvmRuntimeFunction {
    const char *name;
    LLVMValueRef function;
    LLVMTypeRef type;
    /* The C ABI of the result and of each parameter (B3). */
    RLlvmAbiValue result;
    RLlvmAbiValue *parameters;
    uint32_t parameter_count;
    uint32_t *parameter_types; /* runtime surface type of each parameter */
    uint32_t result_type;      /* runtime surface type of the result */
    bool variadic;
} RLlvmRuntimeFunction;

typedef struct RLlvmTypeLayout {
    uint32_t size;
    uint32_t align;
    uint8_t state; /* 0 unknown, 1 being computed, 2 known */
} RLlvmTypeLayout;

/* The LLVM block a MIR edge (source block index, target block id) leaves from. */
typedef struct RLlvmEdge {
    uint32_t source;
    RMirBlockId target;
    LLVMBasicBlockRef block;
} RLlvmEdge;

typedef struct RLlvmStackEdge {
    LLVMValueRef caller;
    LLVMValueRef callee;
} RLlvmStackEdge;

/* A function whose body enters an activation of @recursion(depth = N) (R-FUNC-0026). */
typedef struct RLlvmRecursionMark {
    LLVMValueRef function;
    uint32_t depth;
} RLlvmRecursionMark;

typedef struct RLlvmGlue {
    RTypeId type;
    unsigned kind; /* drop, move, drop cursor or user drop hook */
} RLlvmGlue;

typedef struct RLlvmStackEntry {
    RSymbolId symbol;
    LLVMValueRef global; /* the bound, an i64 constant global set after measurement */
} RLlvmStackEntry;

typedef struct RLlvmEmitter {
    const RFrontendContext *frontend;
    RFrontendStatus status;
    LLVMContextRef context;
    LLVMModuleRef module;
    LLVMBuilderRef builder;
    LLVMTargetMachineRef machine;
    LLVMTargetDataRef data;
    /* Declared runtime and library functions, in order of first use. */
    RLlvmRuntimeFunction *runtime;
    size_t runtime_count;
    size_t runtime_capacity;
    /* The layout of each semantic type, by type id (types.c). */
    RLlvmTypeLayout *layouts;
    /* Program strings by intern id (values.c), static objects by symbol id (statics.c). */
    LLVMValueRef *strings;
    LLVMValueRef *statics;
    /* Drop and move glue by value type id, and the glue whose body is still to be written
       (glue.c). */
    LLVMValueRef *drop_glue;
    LLVMValueRef *move_glue;
    /* The iterative drop of self-nesting types (glue.c): per type its member cursor, member count
       and RRuntimeDropNode, per aggregate the wrapper of its user drop. */
    LLVMValueRef *drop_cursors;
    LLVMValueRef *drop_counts;
    LLVMValueRef *drop_nodes;
    LLVMValueRef *drop_hooks;
    /* Functions r_runtime_drop_iterative calls back (cursors, hooks, leaf drops), for the stack
       bounds (stack.c). */
    LLVMValueRef *runtime_callees;
    size_t runtime_callee_count;
    size_t runtime_callee_capacity;
    /* The hash and equality functions of std.dict keys, per key type (glue.c). */
    LLVMValueRef *key_hashes;
    LLVMValueRef *key_equals;
    /* Calls library code makes back into a function's callees: the key functions a std.dict
       operation hands over, for the stack bounds (stack.c). */
    RLlvmStackEdge *stack_edges;
    size_t stack_edge_count;
    size_t stack_edge_capacity;
    /* The bodies that count activations of @recursion, for the stack bounds (stack.c). */
    RLlvmRecursionMark *recursion_marks;
    size_t recursion_mark_count;
    size_t recursion_mark_capacity;
    RLlvmGlue *glue;
    size_t glue_count;
    size_t glue_capacity;
    /* The functions reachable from the entry, by symbol id (emit.c). */
    uint8_t *reachable;
    /* Ranks of types, symbols and sources independent of the order of the sources (keys.c). */
    uint32_t *type_keys;
    uint32_t *symbol_keys;
    uint32_t *source_keys;
    /* R functions by symbol id. */
    LLVMValueRef *functions;
    LLVMTypeRef *function_types;
    RSymbolId entry_symbol;
    /* The function being lowered. */
    const RMirFunction *mir;
    const RSemanticSymbol *symbol;
    RSymbolId symbol_id;
    LLVMValueRef function;
    /* The LLVM block that starts each MIR block, and the one its terminator ends in (a MIR
       block becomes several LLVM blocks around checks and calls). */
    LLVMBasicBlockRef *blocks;
    LLVMBasicBlockRef *block_ends;
    /* By MIR value id: the defining instruction, and the value: an SSA value for a scalar, the
       address of its memory for any other type. */
    const RMirInstruction **definitions;
    LLVMValueRef *values;
    size_t value_count;
    /* By MIR value id: a value built where its single use moves it (values.c, B6-3), in the
       place an initializing store fills or in a member of the aggregate that takes it. */
    uint8_t *forwarded;
    /* The storage of each local and parameter place. */
    LLVMValueRef *locals;
    RTypeId *local_types;
    size_t local_count;
    LLVMValueRef *parameters;
    RTypeId *parameter_types;
    size_t parameter_count;
    /* The initialization flag (an i8 slot) of each place and value of a type that requires
       drop, NULL for the others (values.c). */
    LLVMValueRef *local_flags;
    LLVMValueRef *parameter_flags;
    LLVMValueRef *value_flags;
    /* Finally routes (emit.c, as the C17 async frame): the stack of entered finally ids, the
       pending completions with their payloads, and the payload types of the function. */
    uint32_t finally_capacity;
    LLVMValueRef finally_stack;
    LLVMValueRef finally_depth;
    LLVMValueRef pending;
    LLVMValueRef pending_depth;
    uint32_t pending_size;
    uint32_t pending_payload_offset;
    RTypeId *pending_types;
    uint32_t pending_type_count;
    /* The slot of each catch payload of a scalar type, by MIR value id. */
    LLVMValueRef *catch_slots;
    /* Frame mode (async.c): while the step of an async function is written, every slot the
       function would allocate on its stack lies in the task frame at `frame` instead, scalar
       values are kept there too (frame_scalars), and the bytes the frame initializer sets to 1
       are recorded (frame_ones). `execution` and `step_result` are the other step parameters. */
    LLVMValueRef frame;
    LLVMValueRef execution;
    LLVMValueRef step_result;
    uint32_t frame_size;
    uint32_t frame_align;
    uint8_t *frame_scalars;
    uint32_t *frame_ones;
    size_t frame_one_count;
    size_t frame_one_capacity;
    /* Per async function: the frame initializer, the frame drop and the frame size (an i64
       constant global set once its step is written). */
    /* The task scopes of the step being written: the frame storage of each scope symbol. */
    RSymbolId *scope_symbols;
    LLVMValueRef *scope_storage;
    LLVMValueRef *scope_entries;
    size_t scope_count;
    size_t scope_capacity;
    LLVMValueRef *async_initializers;
    LLVMValueRef *async_drops;
    /* Owning thread-local objects (statics.c): per symbol the accessor, drop and state byte; the
       per-thread drop stack, its count and its capacity, and r_thread_local_drop_all. */
    LLVMValueRef *thread_local_access;
    LLVMValueRef *thread_local_drop;
    LLVMValueRef *thread_local_state;
    LLVMValueRef thread_local_stack;
    LLVMValueRef thread_local_count;
    LLVMValueRef thread_local_drop_all;
    uint32_t thread_local_capacity;
    /* Per type, its clone glue and whether its body was written (clone.c). */
    LLVMValueRef *clone_glue;
    uint8_t *clone_written;
    /* Per type, its JSON encoder and zero predicate and which bodies were written (bit 0 the
       encoder, bit 1 the predicate; json_encode.c). */
    LLVMValueRef *json_encoders;
    LLVMValueRef *json_zeros;
    /* Per type, its JSON decoder and ordinary default (bits 2 and 3; json_decode.c). */
    LLVMValueRef *json_decoders;
    LLVMValueRef *json_ordinaries;
    LLVMValueRef *json_streams;
    /* The C side of the program (ffi.c): the table of its C types, per symbol the export
       wrapper of an extern "C" definition, the link manifest std.c::link_available reads, the
       options of the artifact (its link manifest) and, per local place of the function, whether
       it is an imported C object, whose reads are checked (R-FFI-0056). */
    struct RLlvmCTypes *c_types;
    LLVMValueRef *c_functions;
    LLVMValueRef link_manifest;
    const RFrontendArtifactOptions *artifact_options;
    uint8_t *local_imports;
    uint8_t *json_written;
    /* Per type, its format glue and whether its body was written (format.c). */
    LLVMValueRef *format_glue;
    uint8_t *format_written;
    /* Per element type of a broadcast that owns resources, its clone adapter (clone.c). */
    LLVMValueRef *broadcast_clones;
    /* Per initializer of std.sync::call_once or get_or_init, its wrapper (sync.c). */
    LLVMValueRef *sync_initializers;
    /* Per thread entry (threads.c): its trampoline, payload move and payload drop. */
    LLVMValueRef *thread_entries;
    LLVMValueRef *thread_moves;
    LLVMValueRef *thread_drops;
    LLVMValueRef *async_frame_sizes;
    /* The phis of the function, completed once every block is lowered, with the MIR block each
       is in. A phi of a value that owns resources is storage its incoming values move to on the
       edges (r_c17_emit_async_phi_edge); a branch reaches such a block through an edge block,
       which then is the predecessor of the block's other phis. */
    const RMirInstruction **phis;
    uint32_t *phi_blocks;
    size_t phi_count;
    uint32_t current_block;
    RLlvmEdge *edges;
    size_t edge_count;
    /* The carrier out pointer of a function whose result is an effect carrier, or the result
       memory of a function whose result lives in memory. */
    LLVMValueRef effect_out;
    LLVMValueRef result_out;
    /* The stack bound of each entry a stack check names (R-FUNC-0004), measured after code
       generation (stack.c). */
    RLlvmStackEntry *stack_entries;
    size_t stack_entry_count;
    size_t stack_entry_capacity;
} RLlvmEmitter;

/* Memory of the emitter, from the frontend allocator. */
void *r_llvm_allocate(RLlvmEmitter *emitter, size_t size);
void r_llvm_free(RLlvmEmitter *emitter, void *pointer);
bool r_llvm_fail_at(RLlvmEmitter *emitter, RFrontendStatus status, const char *file, int line);
/* Records the first failure; an internal error names the place in the emitter that found it. */
#define r_llvm_fail(emitter, status) r_llvm_fail_at((emitter), (status), __FILE__, __LINE__)
/* R_FRONTEND_NOT_LOWERABLE for a construct the emitter does not lower yet; the first one is
   named on stderr with the function it is in. */
bool r_llvm_unsupported(RLlvmEmitter *emitter, const char *what);
bool r_llvm_unsupported_detail(RLlvmEmitter *emitter,
                               const char *what,
                               const char *detail,
                               size_t detail_length);

/* Scalar LLVM types. */
/* The name a test harness gives a runtime function or `main` (--rename-symbol), else `name`. */
const char *r_llvm_renamed(const RLlvmEmitter *emitter, const char *name);
LLVMTypeRef r_llvm_int(RLlvmEmitter *emitter, unsigned bits);
LLVMTypeRef r_llvm_pointer(RLlvmEmitter *emitter);
LLVMValueRef r_llvm_u32(RLlvmEmitter *emitter, uint64_t value);
LLVMValueRef r_llvm_u64(RLlvmEmitter *emitter, uint64_t value);

/* The runtime or library function of that name, declared with its C ABI. */
const RLlvmRuntimeFunction *r_llvm_runtime(RLlvmEmitter *emitter, const char *name);
/* Calls a runtime or library function. Each argument is given at the C level: a scalar as its
   value (a C bool as i1), an aggregate as a pointer to memory holding it. An aggregate result is
   written to `result_memory`; a scalar result is returned. Returns NULL on failure, a dummy value
   for a void or aggregate result. */
LLVMValueRef r_llvm_call_runtime(RLlvmEmitter *emitter,
                                 const char *name,
                                 const LLVMValueRef *arguments,
                                 unsigned argument_count,
                                 LLVMValueRef result_memory);
/* The value of a runtime enumerator or constant. */
bool r_llvm_runtime_constant(RLlvmEmitter *emitter, const char *name, int64_t *value);
/* Size and alignment of a runtime type named by its typedef. */
bool r_llvm_runtime_layout(RLlvmEmitter *emitter,
                           const char *name,
                           uint32_t *size,
                           uint32_t *align);
/* The byte offset of a member of a runtime type named by its typedef. */
bool r_llvm_runtime_field(RLlvmEmitter *emitter,
                          const char *type_name,
                          const char *field_name,
                          uint32_t *offset,
                          uint32_t *field_type);

/* Memory helpers: an alloca in the entry block, a pointer plus a byte offset. */
LLVMValueRef
r_llvm_entry_alloca(RLlvmEmitter *emitter, uint32_t size, uint32_t align, const char *name);
/* In frame mode, the offset in the frame the next slot of that size and alignment takes. */
uint32_t r_llvm_frame_reserve(RLlvmEmitter *emitter, uint32_t size, uint32_t align);
/* The address of a frame offset, computed in the entry block of the current function. */
LLVMValueRef r_llvm_frame_address(RLlvmEmitter *emitter, uint32_t offset);
LLVMValueRef r_llvm_byte_offset(RLlvmEmitter *emitter, LLVMValueRef pointer, uint64_t offset);
/* The pointer plus an offset computed in the entry block, so that it dominates every use; NULL
   when the pointer is defined elsewhere. */
LLVMValueRef r_llvm_entry_offset(RLlvmEmitter *emitter, LLVMValueRef pointer, uint64_t offset);

/* A source span value of the runtime (RRuntimeSourceSpan) in memory. */
LLVMValueRef r_llvm_span(RLlvmEmitter *emitter, RSourceSpan span);

/* Types and layouts of the program (types.c). A scalar value is an SSA value of its scalar type;
   any other value lives in memory with the layout of the C type the C17 emitter declares. */
const RSemanticType *r_llvm_type(const RLlvmEmitter *emitter, RTypeId id);
RTypeId r_llvm_value_type(const RLlvmEmitter *emitter, RTypeId id);
RSemanticTypeKind r_llvm_value_kind(const RLlvmEmitter *emitter, RTypeId id);
bool r_llvm_kind_integer(RSemanticTypeKind kind, unsigned *bits, bool *is_signed);
/* The integer an enumeration without payloads is represented by. */
bool r_llvm_enum_integer(RLlvmEmitter *emitter, RTypeId id, unsigned *bits, bool *is_signed);
/* The LLVM type of a scalar value, NULL for a value in memory. */
LLVMTypeRef r_llvm_scalar_type(RLlvmEmitter *emitter, RTypeId id);
bool r_llvm_type_is_void(RLlvmEmitter *emitter, RTypeId id);
/* The name of a named standard type, or NULL. */
const RInternEntry *r_llvm_standard_name(const RLlvmEmitter *emitter, const RSemanticType *type);
bool r_llvm_standard_named(const RLlvmEmitter *emitter, RTypeId id, const char *name);
/* A named standard type of the move ABI registry: its values own resources. */
bool r_llvm_standard_is_move(const RLlvmEmitter *emitter, const RSemanticType *type);
/* core::atomic_compare_exchange_result<T>: tagged, exchanged (0) and failed (1) both carry T. */
bool r_llvm_compare_exchange_result(const RLlvmEmitter *emitter, RTypeId id);
/* The number of type arguments of a named standard type (0, 1 or 2). */
uint32_t r_llvm_standard_arity(const RSemanticType *type);
/* The C type of a named standard type without type arguments, and its layout. */
const char *r_llvm_standard_c_type(RLlvmEmitter *emitter,
                                   const RSemanticType *type,
                                   uint32_t *size,
                                   uint32_t *align);
const RSemanticAggregate *r_llvm_aggregate(const RLlvmEmitter *emitter, RTypeId id);
const RSemanticField *r_llvm_field(const RLlvmEmitter *emitter, uint32_t field_id);
bool r_llvm_layout(RLlvmEmitter *emitter, RTypeId id, uint32_t *size, uint32_t *align);
/* The offset of the payload union of a tagged type (a carrier, an option, a result, a tagged
   enum): after the 32-bit tag, at the union's alignment. */
bool r_llvm_payload_offset(RLlvmEmitter *emitter, RTypeId id, uint32_t *offset);
bool r_llvm_carrier_payload_offset(RLlvmEmitter *emitter, RTypeId id, uint32_t *offset);
typedef enum RLlvmRecoveringKind {
    R_LLVM_RECOVERING_NONE = 0,
    R_LLVM_RECOVERING_NEW,
    R_LLVM_RECOVERING_PUSH,
    R_LLVM_RECOVERING_INSERT
} RLlvmRecoveringKind;

typedef struct RLlvmRecoveringMembers {
    RTypeId key_type; /* the dict's key, or none */
    RTypeId value_type;
    uint32_t key_offset;
    uint32_t value_offset;
    uint32_t size;
    uint32_t align;
} RLlvmRecoveringMembers;

RLlvmRecoveringKind r_llvm_recovering_error(const RLlvmEmitter *emitter, RTypeId id);
bool r_llvm_recovering_members(RLlvmEmitter *emitter, RTypeId id, RLlvmRecoveringMembers *members);
/* The offset of a field (one-based field id) in its struct. */
bool r_llvm_field_offset(RLlvmEmitter *emitter, uint32_t field_id, uint32_t *offset);

/* Values and places of the function being lowered (values.c). */
bool r_llvm_type_requires_drop(RLlvmEmitter *emitter, RTypeId id);
/* The MIR value: an SSA scalar, or the address of the value's memory. */
LLVMValueRef r_llvm_value(RLlvmEmitter *emitter, RMirValueId id);
bool r_llvm_set_value(RLlvmEmitter *emitter, RMirValueId id, LLVMValueRef value);
/* The memory of a value of a type in memory, allocated once per MIR value. */
LLVMValueRef r_llvm_value_memory(RLlvmEmitter *emitter, RMirValueId id, RTypeId type);
const RMirInstruction *r_llvm_definition(const RLlvmEmitter *emitter, RMirValueId id);
/* A scalar in memory (a bool is a byte) and a value of any type copied between memories. */
LLVMValueRef r_llvm_load_scalar(RLlvmEmitter *emitter, RTypeId type, LLVMValueRef pointer);
void r_llvm_store_scalar(RLlvmEmitter *emitter,
                         RTypeId type,
                         LLVMValueRef value,
                         LLVMValueRef pointer);
bool r_llvm_store_value(RLlvmEmitter *emitter,
                        RTypeId type,
                        LLVMValueRef value,
                        LLVMValueRef pointer);
/* Reads a value of the type from memory into a MIR value. */
bool r_llvm_load_into(RLlvmEmitter *emitter,
                      RMirValueId result,
                      RTypeId type,
                      LLVMValueRef pointer);
/* The address of a place with its projection chain. */
LLVMValueRef r_llvm_place_address(RLlvmEmitter *emitter,
                                  uint32_t ordinal,
                                  bool parameter,
                                  RMirValueId projection);
/* A panic of the category at the span: under unwind it is raised and control continues at the
   panic block, otherwise the process ends (R-ERR-0005, L39). */
bool r_llvm_panic(RLlvmEmitter *emitter,
                  const char *category,
                  RSourceSpan span,
                  RMirBlockId panic_target);
bool r_llvm_check(RLlvmEmitter *emitter,
                  LLVMValueRef failed,
                  const char *category,
                  RSourceSpan span,
                  RMirBlockId panic_target);
bool r_llvm_emit_value_instruction(RLlvmEmitter *emitter, const RMirInstruction *instruction);
/* After a call that may run R code: a pending panic continues at the panic block (L39). */
bool r_llvm_unwind_test(RLlvmEmitter *emitter, RMirBlockId panic_target);
/* The body of a function written from its format recipe (format.c). */
bool r_llvm_define_format_function(RLlvmEmitter *emitter,
                                   RSymbolId id,
                                   const RSemanticSymbol *symbol);
/* The body of an interface or function-value dispatcher (dispatch.c). */
bool r_llvm_define_dispatcher(RLlvmEmitter *emitter, RSymbolId id, const RSemanticSymbol *symbol);
/* Tasks (async.c): a start of an async function, an await in a step, and whether a MOVE is
   left to the frame initializer of a start. */
bool r_llvm_emit_async_start(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_emit_await(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_staged_move(RLlvmEmitter *emitter, const RMirInstruction *instruction);
/* Prepares and commits the task of an async function; its RRuntimeTaskStartResult in memory.
   Mode 0 commits for the executor, 1 may run the first step on this thread, 2 defers it to
   r_runtime_task_run_deferred (a start in a task scope). */
LLVMValueRef
r_llvm_start_task(RLlvmEmitter *emitter, RSymbolId callee, LLVMValueRef context, unsigned mode);
/* TASK_SCOPE_ENTER, TASK_SCOPE_WAIT and TASK_SCOPE_CLOSE in a step (async.c). */
/* VARIANT_TAG and VARIANT_PAYLOAD of a flat standard outcome (outcome.c); *handled is false for
   any other operand. */
bool r_llvm_emit_outcome_variant(RLlvmEmitter *emitter,
                                 const RMirInstruction *instruction,
                                 bool *handled);
/* std.thread and std.async::blocking (threads.c). */
bool r_llvm_thread_spawn(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_thread_join(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_emit_thread_entries(RLlvmEmitter *emitter);
bool r_llvm_thread_handle(RLlvmEmitter *emitter, RTypeId id);
bool r_llvm_thread_join_result(RLlvmEmitter *emitter, RTypeId id);
RTypeId r_llvm_thread_panic_report(RLlvmEmitter *emitter);
bool r_llvm_thread_join_glue(
    RLlvmEmitter *emitter, RTypeId id, bool move, LLVMValueRef first, LLVMValueRef second);
/* The place of a static object, through the accessor of an owning thread-local one; the bodies
   of those accessors and r_thread_local_drop_all (statics.c). */
LLVMValueRef r_llvm_static_place(RLlvmEmitter *emitter, RSymbolId id);
bool r_llvm_emit_thread_locals(RLlvmEmitter *emitter);
bool r_llvm_initialize_static_bytes(RLlvmEmitter *emitter);
/* std.sync (sync.c): the operations, the wrappers of initializers, and the layouts, glue and
   variants of its storages, guards and outcomes. */
bool r_llvm_std_sync(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_std_receive(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_std_async_sync(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_emit_sync_initializers(RLlvmEmitter *emitter);
bool r_llvm_sync_tagged(RLlvmEmitter *emitter, RTypeId id);
RTypeId r_llvm_sync_variant_payload(RLlvmEmitter *emitter, RTypeId id, uint64_t tag);
bool r_llvm_sync_payload_offset_of(RLlvmEmitter *emitter, RTypeId id, uint32_t *offset);
bool r_llvm_sync_layout(
    RLlvmEmitter *emitter, RTypeId id, uint32_t *size, uint32_t *align, bool *handled);
bool r_llvm_sync_glue(RLlvmEmitter *emitter,
                      RTypeId id,
                      bool move,
                      LLVMValueRef first,
                      LLVMValueRef second,
                      bool *handled);
/* core::clone and the clone glue of R-OWN-0020 (clone.c). */
bool r_llvm_std_clone(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_emit_clone_glue(RLlvmEmitter *emitter);
bool r_llvm_emit_format_glue(RLlvmEmitter *emitter);
/* JSON operations of std.json whose body the backend writes (json.c). */
bool r_llvm_json_supported(uint32_t operation);
bool r_llvm_define_json_function(RLlvmEmitter *emitter,
                                 RSymbolId id,
                                 const RSemanticSymbol *symbol);
bool r_llvm_json_carrier(RLlvmEmitter *emitter,
                         const RSemanticSymbol *symbol,
                         LLVMValueRef carrier,
                         LLVMValueRef native,
                         const char *result_type,
                         bool consumes);
bool r_llvm_json_default_options(RLlvmEmitter *emitter, LLVMValueRef options);
uint32_t r_llvm_json_field_key(const RSemanticField *field);
const RSemanticAggregate *r_llvm_json_aggregate(RLlvmEmitter *emitter, RTypeId id);
/* JSON encoders and zero predicates per type (json_encode.c). */
bool r_llvm_define_json_marshal(RLlvmEmitter *emitter, RSymbolId id, const RSemanticSymbol *symbol);
bool r_llvm_emit_json_encoders(RLlvmEmitter *emitter);
/* JSON decoders and ordinary defaults per type (json_decode.c). */
bool r_llvm_define_json_unmarshal(RLlvmEmitter *emitter,
                                  RSymbolId id,
                                  const RSemanticSymbol *symbol);
bool r_llvm_emit_json_decoders(RLlvmEmitter *emitter);
/* Stream decoders and readers of std.json (json_stream.c). */
bool r_llvm_define_json_stream(RLlvmEmitter *emitter, RSymbolId id, const RSemanticSymbol *symbol);
bool r_llvm_emit_json_streams(RLlvmEmitter *emitter);
/* Imports, raw fn calls, export wrappers, imported objects and link_available (ffi.c). */
bool r_llvm_ffi_function_address(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_ffi_indirect_call(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_ffi_link_available(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_ffi_std_c(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_ffi_ingress(RLlvmEmitter *emitter, RTypeId type, LLVMValueRef value, RSourceSpan span);
LLVMValueRef r_llvm_ffi_object(RLlvmEmitter *emitter, RSymbolId id);
bool r_llvm_emit_c_exports(RLlvmEmitter *emitter);
void r_llvm_ffi_release(RLlvmEmitter *emitter);
/* Marks a call through a pointer whose callees the caller named as stack edges (stack.c). */
void r_llvm_mark_indirect_call(RLlvmEmitter *emitter, LLVMValueRef call);
bool r_llvm_indirect_call_marked(RLlvmEmitter *emitter, LLVMValueRef call);
/* The C ABI helpers of runtime.c. */
LLVMTypeRef
r_llvm_abi_register_type(RLlvmEmitter *emitter, const RLlvmAbiValue *value, bool result);
void r_llvm_add_extension(RLlvmEmitter *emitter,
                          LLVMValueRef target,
                          LLVMAttributeIndex index,
                          RLlvmAbiExtend extend,
                          bool call_site);
LLVMAttributeRef r_llvm_sret_attribute(RLlvmEmitter *emitter, uint32_t size);
bool r_llvm_std_core_format(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_format_glue_member(RLlvmEmitter *emitter,
                               RTypeId member,
                               LLVMValueRef carrier,
                               RTypeId carrier_type,
                               LLVMValueRef value,
                               LLVMValueRef out);
LLVMValueRef r_llvm_broadcast_clone(RLlvmEmitter *emitter, RTypeId type);
/* std.async::broadcast_result and std.arc/std.rc::try_unwrap_result (tagged.c). */
bool r_llvm_standard_tagged(RLlvmEmitter *emitter, RTypeId id);
RTypeId r_llvm_standard_tagged_payload(RLlvmEmitter *emitter, RTypeId id, uint64_t tag);
bool r_llvm_standard_tagged_union(RLlvmEmitter *emitter,
                                  RTypeId id,
                                  uint32_t *size,
                                  uint32_t *align);
bool r_llvm_standard_tagged_glue(
    RLlvmEmitter *emitter, RTypeId id, bool move, LLVMValueRef first, LLVMValueRef second);
/* std.error::from_fault (hosted_main.c, R-SLIB-ERR-0004). */
bool r_llvm_fault_portable(RLlvmEmitter *emitter, const RMirInstruction *instruction);
bool r_llvm_emit_task_scope(RLlvmEmitter *emitter, const RMirInstruction *instruction);
/* A STANDARD_CALL instruction (standard.c). */
bool r_llvm_emit_standard_call(RLlvmEmitter *emitter, const RMirInstruction *instruction);
/* The private constant holding a program string (an intern id). */
LLVMValueRef r_llvm_program_string(RLlvmEmitter *emitter, uint32_t intern_id);
LLVMValueRef r_llvm_empty_string(RLlvmEmitter *emitter);
/* The offset of the tag in an owner of an interface, struct { <owner> r_owner; uint32_t r_tag; }.
 */
bool r_llvm_dyn_owner_tag_offset(RLlvmEmitter *emitter, RTypeId type, uint32_t *offset);
/* The global of a module object or static local (statics.c). */
LLVMValueRef r_llvm_static_global(RLlvmEmitter *emitter, RSymbolId id);
/* The drops of the static objects that own resources, at the end of a hosted program. */
bool r_llvm_emit_static_drops(RLlvmEmitter *emitter);
/* The slot a THROW to a catch in the same function writes and its EFFECT_PAYLOAD reads. */
LLVMValueRef r_llvm_catch_slot(RLlvmEmitter *emitter, RMirValueId id, RTypeId type);

/* Type glue (glue.c): calls of the drop and the move of a value of the type, and the bodies of
   the glue the program uses, written after its functions. */
bool r_llvm_call_drop(RLlvmEmitter *emitter, RTypeId type, LLVMValueRef pointer);
bool r_llvm_call_move(RLlvmEmitter *emitter,
                      RTypeId type,
                      LLVMValueRef destination,
                      LLVMValueRef source);
bool r_llvm_emit_pending_glue(RLlvmEmitter *emitter);
/* A RRuntimeTypeInfo of the type in memory, for the runtime calls that take one. */
LLVMValueRef r_llvm_type_info(RLlvmEmitter *emitter, RTypeId type);
/* The hash (`uint64_t (*)(const void *)`) or equality (`_Bool (*)(const void *, const void *)`)
   function of a std.dict key type, as the C17 emitter's r_core_key_hash and r_core_key_equal. */
LLVMValueRef r_llvm_key_function(RLlvmEmitter *emitter, RTypeId key, bool hash);

/* Initialization flags (values.c): the flag of a place or value that requires drop, the drop of
   what a flag says is initialized, and the guarded move between two flagged storages. */
LLVMValueRef r_llvm_place_flag(RLlvmEmitter *emitter, uint32_t ordinal, bool parameter);
LLVMValueRef r_llvm_value_flag(RLlvmEmitter *emitter, RMirValueId id);
void r_llvm_set_flag(RLlvmEmitter *emitter, LLVMValueRef flag, bool value);
bool r_llvm_drop_flagged(RLlvmEmitter *emitter,
                         RTypeId type,
                         LLVMValueRef pointer,
                         LLVMValueRef flag);
bool r_llvm_transfer(RLlvmEmitter *emitter,
                     RTypeId type,
                     LLVMValueRef destination,
                     LLVMValueRef destination_flag,
                     LLVMValueRef source,
                     LLVMValueRef source_flag);

/* r_runtime_unwinding() at the insertion point, as an i1. */
LLVMValueRef r_llvm_unwinding(RLlvmEmitter *emitter);

/* The hosted entry `main` (hosted_main.c) and the R entry it calls. */
RSymbolId r_llvm_entry(RLlvmEmitter *emitter);
bool r_llvm_emit_hosted_main(RLlvmEmitter *emitter);

/* Stack bounds (stack.c): the bound of an entry as a value at the insertion point, and the
   measurement that sets every bound once the module is complete. */
LLVMValueRef r_llvm_stack_entry(RLlvmEmitter *emitter, RSymbolId symbol);
/* The entry stack budget of the target manifest (target.c). */
uint64_t r_llvm_target_entry_budget(void);
bool r_llvm_measure_stack(RLlvmEmitter *emitter);
/* Records that library code called from `caller` may call `callee` back. */
bool r_llvm_add_stack_edge(RLlvmEmitter *emitter, LLVMValueRef caller, LLVMValueRef callee);
/* Records that `function` counts at most `depth` activations of itself (@recursion). */
bool r_llvm_stack_recursion(RLlvmEmitter *emitter, LLVMValueRef function, uint32_t depth);

/* Order-independent keys (keys.c, B6-2): the one-based rank of a type, symbol or source among all
   of its kind by a text that does not depend on the order of the sources; every name the emitter
   numbers and every source it reports uses them instead of semantic ids. */
bool r_llvm_prepare_keys(RLlvmEmitter *emitter);
void r_llvm_release_keys(RLlvmEmitter *emitter);
uint32_t r_llvm_type_key(const RLlvmEmitter *emitter, RTypeId id);
uint32_t r_llvm_symbol_key(const RLlvmEmitter *emitter, RSymbolId id);
uint32_t r_llvm_source_key(const RLlvmEmitter *emitter, RSourceId id);

/* B6-3: the memory of a local that lives inside the carrier of the checked call initializing it,
   or NULL (values.c). */
LLVMValueRef r_llvm_local_in_carrier(RLlvmEmitter *emitter, const RMirInstruction *local);

#endif
