#ifndef R_FRONTEND_H
#define R_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R_FRONTEND_VERSION "0.1.0"
#define R_FRONTEND_CORE_REVISION "0.1.0-draft.109"

typedef uint32_t RSourceId;
typedef uint32_t RTokenId;
typedef uint32_t RSyntaxNodeId;
typedef uint32_t RAstNodeId;
typedef uint32_t RTypeId;
typedef uint32_t RSymbolId;
typedef uint32_t RHirNodeId;
typedef uint32_t RMirFunctionId;
typedef uint32_t RMirBlockId;
typedef uint32_t RMirValueId;

#define R_SOURCE_ID_INVALID UINT32_C(0)
#define R_TOKEN_ID_INVALID UINT32_C(0)
#define R_SYNTAX_NODE_ID_INVALID UINT32_C(0)
#define R_AST_NODE_ID_INVALID UINT32_C(0)
#define R_TYPE_ID_INVALID UINT32_C(0)
#define R_SYMBOL_ID_INVALID UINT32_C(0)
#define R_HIR_NODE_ID_INVALID UINT32_C(0)
#define R_MIR_FUNCTION_ID_INVALID UINT32_C(0)
#define R_MIR_BLOCK_ID_INVALID UINT32_C(0)
#define R_MIR_VALUE_ID_INVALID UINT32_C(0)

typedef struct RSourceSpan {
    RSourceId source;
    uint32_t start;
    uint32_t end;
} RSourceSpan;

typedef enum RFrontendStatus {
    R_FRONTEND_OK = 0,
    R_FRONTEND_INVALID_ARGUMENT,
    R_FRONTEND_INVALID_SOURCE,
    R_FRONTEND_NOT_LOWERABLE,
    R_FRONTEND_OUT_OF_MEMORY,
    R_FRONTEND_LIMIT_EXCEEDED,
    R_FRONTEND_IO_ERROR,
    R_FRONTEND_INTERNAL_ERROR,
    R_FRONTEND_CONTEXT_SEALED
} RFrontendStatus;

typedef enum RDiagnosticSeverity {
    R_DIAGNOSTIC_WARNING = 0,
    R_DIAGNOSTIC_ERROR = 1
} RDiagnosticSeverity;

typedef enum RDiagnosticPhase {
    R_DIAGNOSTIC_PHASE_LEXICAL = 0,
    R_DIAGNOSTIC_PHASE_SYNTAX,
    R_DIAGNOSTIC_PHASE_SEMANTIC,
    R_DIAGNOSTIC_PHASE_OWNERSHIP
} RDiagnosticPhase;

typedef struct RDiagnostic {
    const char *code;
    const char *rule_id;
    const char *message;
    RDiagnosticSeverity severity;
    RDiagnosticPhase phase;
    RSourceSpan primary_span;
} RDiagnostic;

typedef struct RFrontendLimits {
    size_t max_source_bytes;
    uint32_t max_nesting;
    uint32_t max_diagnostics;
} RFrontendLimits;

typedef void *(*RFrontendAllocateFn)(void *user_data, size_t size);
typedef void (*RFrontendFreeFn)(void *user_data, void *pointer);

typedef struct RFrontendOptions {
    RFrontendLimits limits;
    RFrontendAllocateFn allocate;
    RFrontendFreeFn free;
    void *allocator_user_data;
} RFrontendOptions;

typedef bool (*RFrontendWriteFn)(void *user_data, const char *bytes, size_t length);

typedef struct RFrontendContext RFrontendContext;

/*
 * Compiler artifact selection is deliberately byte-oriented. All pointers are borrowed
 * and shall remain valid for the duration of the dump call. Manifest contents are never
 * interpreted as host paths, keeping emitted artifacts independent of checkout location.
 */
/* The sanitizer an emitted program is instrumented for (B8.3): the LLVM pass of the sanitizer
   runs over every function the object defines after the optimizer and before the stack bounds
   are measured, and the program links the sanitizer's runtime. R code needs no undefined-behaviour
   sanitizer: its checks are explicit (R-EXPR-0005, R-EXPR-0021). */
typedef enum RFrontendSanitizer {
    R_FRONTEND_SANITIZER_NONE = 0,
    R_FRONTEND_SANITIZER_ADDRESS,
    R_FRONTEND_SANITIZER_THREAD
} RFrontendSanitizer;

typedef struct RFrontendArtifactOptions {
    const char *entry;
    const char *profile;
    const uint8_t *target_manifest;
    size_t target_manifest_length;
    const uint8_t *link_manifest;
    size_t link_manifest_length;
    /* "OLD=NEW" pairs a test harness gives the LLVM backend: a call of the runtime or library
       function OLD calls NEW instead, and the program's `main` is NEW where OLD is "main". */
    const char *const *symbol_renames;
    size_t symbol_rename_count;
    /* The LLVM backend lowers the functions the entry reaches; with all_functions it lowers every
       function of the program, as acceptance audits need for code no entry calls, and keeps each
       through the optimizer as if called from elsewhere. */
    bool all_functions;
    /* The LLVM pipeline default<O1>..default<O3> for levels 1-3; 0 leaves the module as the
       emitter writes it. */
    uint32_t optimization_level;
    /* Above level 0, the bitcode of the runtime and library C a program calls (B7.2): given the
       name of a C function, the loader sets *data and *length to a bitcode module that defines
       it, valid until the emission returns, and returns true; it returns false when it has none.
       The definitions are optimized together with the program and still linked from the
       archives. */
    bool (*load_bitcode)(void *user_data, const char *symbol, const uint8_t **data, size_t *length);
    void *bitcode_user_data;
    /* Above level 0, profile-guided optimization (B7.2): profile_generate instruments the program
       for a profile it writes when it ends (linked with the profile runtime of the toolchain);
       profile_use names a merged profile (llvm-profdata) of such runs that guides the optimizer.
       LLVM reads the profile path once per process, so every emission of a process that uses a
       profile names the same file. */
    bool profile_generate;
    const char *profile_use;
    RFrontendSanitizer sanitizer;
    /* DWARF of the program (B8.1): files, functions with their R names, source lines and
       columns, parameters and locals with their R types. */
    bool debug_info;
    /* Line coverage of the R source (B8.2): the program counts the lines it runs in the gcov
       format; the notes are written to PREFIX.gcno when the program is emitted, the counts to
       PREFIX.gcda when it ends (linked with the profile runtime of the toolchain). Implies
       debug_info, whose lines the counts refer to. */
    const char *coverage_prefix;
    /* A library (B10.1, R-FFI-0064): the program has no main; its exports are its API, and the
       emitter defines NAME_initialize and NAME_shutdown, which start and stop the runtime and
       the program's static state. NULL for a program with a main. */
    const char *library_name;
    /* A unit of the runtime written in R (B12, r_frontend_set_runtime_unit): no main and no
       library entries; the exports are the program's entry points, emitted as plain C
       functions. */
    bool runtime_unit;
} RFrontendArtifactOptions;

RFrontendOptions r_frontend_default_options(void);
RFrontendContext *r_frontend_create(const RFrontendOptions *options);
void r_frontend_destroy(RFrontendContext *context);

/*
 * Selects the library profile before analysis (R-CONF-G005). Accepted names are freestanding,
 * allocation, hosted, hosted-thread and hosted-native-async; a fresh context uses
 * hosted-native-async. An unknown name is rejected with R_FRONTEND_INVALID_ARGUMENT and a
 * selection after analysis has begun with R_FRONTEND_CONTEXT_SEALED; neither changes the context.
 */
/* The code generation backend: the linked LLVM release as "MAJOR.MINOR.PATCH", the LLVM triple
   and CPU of the target manifest, and a check that the linked LLVM is the manifest toolchain's and
   that its target machine has the manifest data layout (false with a reason in `message`). */
bool r_frontend_backend_version(char *buffer, size_t capacity);
const char *r_frontend_backend_triple(void);
const char *r_frontend_backend_cpu(void);
bool r_frontend_backend_verify(char *message, size_t capacity);

RFrontendStatus r_frontend_set_profile(RFrontendContext *context, const char *profile);

/*
 * B12: the translation is a unit of the runtime written in R. Its extern "C" definitions are the
 * runtime's own functions: they may take the implementation's `r_runtime_` names, each is proved
 * against the declaration of a runtime header, and they are emitted as plain C functions (no
 * entry checks, panics that abort). Requires the freestanding profile. A selection after analysis
 * has begun is rejected with R_FRONTEND_CONTEXT_SEALED.
 */
RFrontendStatus r_frontend_set_runtime_unit(RFrontendContext *context);

/*
 * Applies the deny-panic-allocation policy of R-OBJ-0012 to every translation unit, as if each
 * module declaration carried @deny_panic_alloc. A selection after analysis has begun is rejected
 * with R_FRONTEND_CONTEXT_SEALED and changes nothing.
 */
RFrontendStatus r_frontend_set_deny_panic_alloc(RFrontendContext *context, bool deny);

/*
 * Selects the test mode of R-FUNC-0025: the entry module gets an entry point that runs its test
 * functions. entry_module names that module; NULL selects the first source that is added. The
 * selection precedes every source; afterwards it is rejected with R_FRONTEND_CONTEXT_SEALED and
 * changes nothing.
 */
RFrontendStatus r_frontend_set_test_mode(RFrontendContext *context, const char *entry_module);

RFrontendStatus r_frontend_add_source(RFrontendContext *context,
                                      const char *display_name,
                                      const uint8_t *bytes,
                                      size_t length,
                                      RSourceId *source_id);
/* Adds a standard module written in R (R-MOD-0002): the source may declare a module under the
 * reserved core or std roots and is importable from `minimum_profile` upwards, spelled like the
 * --profile names. */
RFrontendStatus r_frontend_add_library_source(RFrontendContext *context,
                                              const char *display_name,
                                              const uint8_t *bytes,
                                              size_t length,
                                              const char *minimum_profile,
                                              RSourceId *source_id);
RFrontendStatus r_frontend_lex(RFrontendContext *context, RSourceId source_id);
RFrontendStatus r_frontend_scan_interface(RFrontendContext *context, RSourceId source_id);
RFrontendStatus r_frontend_link_interfaces(RFrontendContext *context);
RFrontendStatus
r_frontend_parse_cst(RFrontendContext *context, RSourceId source_id, RSyntaxNodeId *root_node);
RFrontendStatus
r_frontend_lower_ast(RFrontendContext *context, RSourceId source_id, RAstNodeId *root_node);
RFrontendStatus r_frontend_analyze(RFrontendContext *context);

/*
 * Resolves every extern "C" import against the link manifest (R-FFI-0030..0036) after a
 * successful analysis. A program without imports accepts a NULL manifest. Provider, kind and
 * symbol-inventory mismatches are reported as R-DIAG-LINK diagnostics with
 * R_FRONTEND_INVALID_SOURCE; a malformed manifest returns R_FRONTEND_INVALID_ARGUMENT.
 */
/*
 * Library R-SLIB-RSRC-0003 (M25): whether a standard module written in R that the analyzed
 * program uses declares an extern "C" block. Its providers are entries of the link manifest of
 * the library, which the implementation reads together with the manifest of the program.
 */
bool r_frontend_uses_library_natives(const RFrontendContext *context);

/*
 * The bytes of a valid link manifest between the brackets of its links array, without the white
 * space next to them, so that the entries of two manifests can be read as one; false when the
 * manifest is not valid.
 */
bool r_frontend_link_manifest_links(const uint8_t *manifest,
                                    size_t manifest_length,
                                    size_t *first,
                                    size_t *last);

/* Whether two valid link manifests have an entry with the same logical name. */
bool r_frontend_link_manifests_share_name(const uint8_t *first,
                                          size_t first_length,
                                          const uint8_t *second,
                                          size_t second_length);

RFrontendStatus r_frontend_resolve_links(RFrontendContext *context,
                                         const uint8_t *link_manifest,
                                         size_t link_manifest_length);
/*
 * Loads one ABI record document (schema r-abi-record-0.1) written by
 * r_frontend_emit_abi_record. A malformed document returns R_FRONTEND_INVALID_ARGUMENT.
 */
RFrontendStatus r_frontend_load_abi_records(RFrontendContext *context,
                                            const uint8_t *document,
                                            size_t document_length);

/*
 * R-CONF-G005: whether `manifest` is, byte for byte, the committed target manifest this
 * implementation binds to the freestanding profile or to the hosted profiles.
 */
bool r_frontend_target_manifest_supported(const uint8_t *manifest,
                                          size_t manifest_length,
                                          bool freestanding);

/*
 * Proves every complete @repr(C) aggregate declared inside an extern "C" block against the
 * loaded ABI records (R-FFI-0017, R-FFI-0041), and every used record against the block's
 * provider, the target manifest's target_triple (when a manifest is given) and C17 options
 * (R-FFI-0040). A mismatch or a missing record is reported as R-DIAG-FFI-004 and returns
 * R_FRONTEND_INVALID_SOURCE. Requires r_frontend_resolve_links.
 */
RFrontendStatus r_frontend_verify_abi_records(RFrontendContext *context,
                                              const uint8_t *target_manifest,
                                              size_t target_manifest_length);

/*
 * Header source for R-FFI-0044 digest verification. read yields the bytes of the header that an
 * ABI record names by spelling, searched through the header roots the build verifies against, or
 * returns false when no root provides it; release returns the buffer once it has been hashed.
 */
typedef struct RFrontendAbiHeaderSource {
    bool (*read)(void *user_data,
                 const char *spelling,
                 size_t spelling_length,
                 uint8_t **bytes,
                 size_t *length);
    void (*release)(void *user_data, uint8_t *bytes);
    void *user_data;
} RFrontendAbiHeaderSource;

/*
 * Compares every header digest recorded by the ABI records that the program's complete C
 * aggregates depend on against the headers the source yields (R-FFI-0044). A header no root
 * provides or a digest that disagrees is R-DIAG-FFI-004 and returns R_FRONTEND_INVALID_SOURCE.
 * Requires r_frontend_verify_abi_records.
 */
RFrontendStatus r_frontend_verify_abi_record_headers(RFrontendContext *context,
                                                     const RFrontendAbiHeaderSource *source);

/*
 * Writes the ABI inventory request (schema r-abi-inventory-request-0.1): for every extern "C"
 * block naming an @abi record, its provider, feature-test definitions, headers and the C types
 * whose inventories the record shall contain. Requires r_frontend_resolve_links.
 */
RFrontendStatus r_frontend_emit_abi_inventory(const RFrontendContext *context,
                                              const RFrontendArtifactOptions *options,
                                              RFrontendWriteFn writer,
                                              void *user_data);

RHirNodeId r_frontend_hir_root(const RFrontendContext *context);
RFrontendStatus r_frontend_lower_mir(RFrontendContext *context);
size_t r_frontend_source_import_count(const RFrontendContext *context, RSourceId source_id);
const char *r_frontend_source_import_module(const RFrontendContext *context,
                                            RSourceId source_id,
                                            size_t import_index);

size_t r_frontend_source_count(const RFrontendContext *context);
const char *r_frontend_source_name(const RFrontendContext *context, RSourceId source_id);
const char *r_frontend_source_module_name(const RFrontendContext *context, RSourceId source_id);
/* R-FUNC-0008: the number of exported entry points of an analyzed program, each of the form
 * `i32 main()`, `i32 main(const str[] args)` or their async forms. */
size_t r_frontend_entry_point_count(const RFrontendContext *context);
size_t r_frontend_diagnostic_count(const RFrontendContext *context);
const RDiagnostic *r_frontend_diagnostic(const RFrontendContext *context, size_t index);
void r_frontend_source_position(const RFrontendContext *context,
                                RSourceSpan span,
                                uint32_t *line,
                                uint32_t *column);

RFrontendStatus r_frontend_dump_tokens(const RFrontendContext *context,
                                       RSourceId source_id,
                                       RFrontendWriteFn writer,
                                       void *user_data);
RFrontendStatus r_frontend_dump_cst(const RFrontendContext *context,
                                    RSourceId source_id,
                                    RFrontendWriteFn writer,
                                    void *user_data);
RFrontendStatus r_frontend_dump_ast(const RFrontendContext *context,
                                    RSourceId source_id,
                                    RFrontendWriteFn writer,
                                    void *user_data);
RFrontendStatus
r_frontend_dump_hir(const RFrontendContext *context, RFrontendWriteFn writer, void *user_data);
RFrontendStatus
r_frontend_dump_mir(const RFrontendContext *context, RFrontendWriteFn writer, void *user_data);
RFrontendStatus r_frontend_dump_interface(const RFrontendContext *context,
                                          const RFrontendArtifactOptions *options,
                                          RFrontendWriteFn writer,
                                          void *user_data);
RFrontendStatus r_frontend_dump_link_plan(const RFrontendContext *context,
                                          const RFrontendArtifactOptions *options,
                                          RFrontendWriteFn writer,
                                          void *user_data);
RFrontendStatus r_frontend_dump_bundle(const RFrontendContext *context,
                                       const RFrontendArtifactOptions *options,
                                       RFrontendWriteFn writer,
                                       void *user_data);
/*
 * The LLVM emitter (LLVM transition, B4): the program as one LLVM module for the manifest target,
 * written as textual IR or as an object file of the target. Requires r_frontend_lower_mir;
 * R_FRONTEND_NOT_LOWERABLE for a construct the emitter does not lower yet.
 */
typedef enum RFrontendLlvmOutput {
    R_FRONTEND_LLVM_IR = 0,
    R_FRONTEND_LLVM_OBJECT
} RFrontendLlvmOutput;
/* B10.1 (R-FFI-0064): the link of a library into a shared library of the target. linker is the
   C compiler driver of the target manifest's toolchain (its digest is checked), inputs the
   archives and objects of the runtime and the library, the versions "X[.Y[.Z]]". */
typedef struct RFrontendLibraryLink {
    const char *linker;
    const char *sysroot;
    const char *output;
    const char *install_name;
    const char *current_version;
    const char *compatibility_version;
    const char *const *inputs;
    size_t input_count;
} RFrontendLibraryLink;

typedef struct RFrontendLibraryLinkResult {
    bool linker_unproved;
    bool link_failed;
    /* R-CMAP-0031: an export the result lacks, or a symbol it exports that is no export. */
    bool missing;
    bool extra;
    char symbol[256];
} RFrontendLibraryLinkResult;

/* The exported symbols of a library, with the target's global prefix, one per line. */
RFrontendStatus r_frontend_emit_export_list(const RFrontendContext *context,
                                            const RFrontendArtifactOptions *options,
                                            RFrontendWriteFn writer,
                                            void *user_data);
/* The suffix (_initialize or _shutdown) that an export of the program takes from the library
   NAME, or NULL. */
const char *r_frontend_library_conflict(const RFrontendContext *context, const char *name);
RFrontendStatus r_frontend_link_library(RFrontendContext *context,
                                        const RFrontendArtifactOptions *options,
                                        const RFrontendLibraryLink *link,
                                        RFrontendLibraryLinkResult *result);

/* B10.2 (R-FFI-0065): what keeps the API of the library NAME from a C declaration (a type
   without one, a keyword of C or C++, a name declared twice), described in buffer; NULL when
   nothing does. Requires the semantic analysis. */
const char *r_frontend_library_api_problem(const RFrontendContext *context,
                                           const char *name,
                                           char *buffer,
                                           size_t capacity);

/* B10.2 (R-FFI-0066): the ABI baseline of a library (schema r-abi-baseline-0.1): its name,
   target and versions, each export's C prototype and each R-declared type's layout. */
RFrontendStatus r_frontend_emit_abi_baseline(const RFrontendContext *context,
                                             const RFrontendArtifactOptions *options,
                                             const char *version,
                                             const char *compatibility_version,
                                             RFrontendWriteFn writer,
                                             void *user_data);

/* The verdict of a build of a library on an earlier ABI baseline (R-FFI-0066). */
typedef enum RFrontendAbiBaselineVerdict {
    /* The compatibility versions are equal and every export and type of the baseline is kept. */
    R_FRONTEND_ABI_BASELINE_KEPT = 0,
    /* The compatibility version is higher than the baseline's: the build may break it. */
    R_FRONTEND_ABI_BASELINE_ADMITTED,
    /* The document is no baseline of this library and target. */
    R_FRONTEND_ABI_BASELINE_FOREIGN,
    /* The compatibility version is lower than the baseline's. */
    R_FRONTEND_ABI_BASELINE_OLDER,
    /* An export or type of the baseline is gone, or changed its prototype or layout. */
    R_FRONTEND_ABI_BASELINE_REMOVED,
    R_FRONTEND_ABI_BASELINE_CHANGED
} RFrontendAbiBaselineVerdict;

typedef struct RFrontendAbiBaselineResult {
    RFrontendAbiBaselineVerdict verdict;
    /* The export, or with subject_is_type the type, a REMOVED or CHANGED verdict names. */
    char subject[256];
    bool subject_is_type;
} RFrontendAbiBaselineResult;

/* Compares the library with the baseline document of an earlier build; a malformed document is
   the FOREIGN verdict, not a failure of the call. */
RFrontendStatus r_frontend_check_abi_baseline(const RFrontendContext *context,
                                              const RFrontendArtifactOptions *options,
                                              const char *version,
                                              const char *compatibility_version,
                                              const uint8_t *baseline,
                                              size_t baseline_length,
                                              RFrontendAbiBaselineResult *result);

RFrontendStatus r_frontend_emit_llvm(const RFrontendContext *context,
                                     const RFrontendArtifactOptions *options,
                                     RFrontendLlvmOutput output,
                                     RFrontendWriteFn writer,
                                     void *user_data);

/*
 * ABI verifier translation unit for every extern "C" import (R-FFI-0042): the providers'
 * feature-test definitions, the blocks' headers and one warnings-as-errors function-pointer
 * assignment per import. It is compiled for the target with the generated-C options and never
 * executed; a compile failure is the R-DIAG-FFI-004 outcome. Requires r_frontend_resolve_links.
 */
RFrontendStatus r_frontend_emit_abi_verifier(const RFrontendContext *context,
                                             const RFrontendArtifactOptions *options,
                                             RFrontendWriteFn writer,
                                             void *user_data);

/*
 * The C headers of the program's imports are parsed in process by libclang for the target
 * manifest's LLVM triple (B9.1): sysroot is the system root of the pinned SDK, roots the header
 * roots of the providers (the -I directories of a C build).
 */
typedef struct RFrontendHeaderOptions {
    const char *sysroot;
    const char *const *roots;
    size_t root_count;
} RFrontendHeaderOptions;

/*
 * The release of the linked libclang ("22.1.8"); false when it reports none or the buffer is
 * too small.
 */
bool r_frontend_header_version(char *buffer, size_t capacity);

/*
 * R-FFI-0042 in process: parses the verifier translation unit of r_frontend_emit_abi_verifier
 * with libclang, with the C17 options and warnings as errors of the generated-C contract, against
 * the providers' feature-test definitions and the blocks' headers. A disagreement is
 * R-DIAG-FFI-004 on the R declaration whose probe failed and returns R_FRONTEND_INVALID_SOURCE;
 * libclang's own diagnostics are kept as the header report. A sysroot that is not the SDK the
 * target manifest pins, or a libclang of another release, returns R_FRONTEND_INVALID_ARGUMENT.
 * Requires r_frontend_resolve_links and r_frontend_verify_abi_records.
 */
RFrontendStatus r_frontend_verify_headers(RFrontendContext *context,
                                          const RFrontendHeaderOptions *options);

/*
 * B10.2 (R-FFI-0065): writes the C header of a library (artifact options with library_name):
 * NAME_initialize and NAME_shutdown, the exports, and the R-declared types they reach with
 * assertions of their layout, version the optional NAME_VERSION. The header is parsed by libclang
 * for the target with the options of the generated-C contract before it is written; its
 * diagnostics are the header report and return R_FRONTEND_INVALID_SOURCE. An API without a C
 * declaration (r_frontend_library_api_problem) returns R_FRONTEND_NOT_LOWERABLE.
 */
RFrontendStatus r_frontend_emit_c_header(RFrontendContext *context,
                                         const RFrontendArtifactOptions *artifact_options,
                                         const RFrontendHeaderOptions *options,
                                         const char *version,
                                         RFrontendWriteFn writer,
                                         void *user_data);

/*
 * Writes the ABI records (schema r-abi-record-0.1) of every extern "C" block that names one with
 * @abi, derived by libclang from the block's headers for the target (R-FFI-0040, R-FFI-0041):
 * the inventories and layouts of the requested C types, the C types of the imports that pass
 * R-declared @repr(C) structs and the inventories reached from them. A record that cannot be
 * derived returns R_FRONTEND_INVALID_SOURCE with the reason in the header report. Requires
 * r_frontend_resolve_links.
 */
RFrontendStatus r_frontend_emit_abi_record(RFrontendContext *context,
                                           const RFrontendArtifactOptions *artifact_options,
                                           const RFrontendHeaderOptions *options,
                                           RFrontendWriteFn writer,
                                           void *user_data);

/*
 * B9.2 (R-FFI-0009): writes the program's only module that has one empty extern "C" block with
 * @header, with the block filled with the declarations of what its headers declare themselves,
 * parsed by libclang for the target: structs (complete when the block names an @abi record),
 * enumerations, integer constants of macros and enumerators, objects and functions, and a
 * comment for each declaration R cannot express. The output is source to review. A module
 * without such a block returns R_FRONTEND_INVALID_SOURCE with the reason in the header report.
 */
RFrontendStatus r_frontend_emit_bindings(RFrontendContext *context,
                                         const RFrontendHeaderOptions *options,
                                         RFrontendWriteFn writer,
                                         void *user_data);

/* The diagnostics of the last r_frontend_verify_headers or r_frontend_emit_abi_record, or NULL. */
const char *r_frontend_header_report(const RFrontendContext *context, size_t *length);

RFrontendStatus r_frontend_reconstruct_source(const RFrontendContext *context,
                                              RSourceId source_id,
                                              RFrontendWriteFn writer,
                                              void *user_data);
RFrontendStatus r_frontend_dump_diagnostics_json(const RFrontendContext *context,
                                                 RFrontendWriteFn writer,
                                                 void *user_data);

#ifdef __cplusplus
}
#endif

#endif
