#ifndef R_FRONTEND_H
#define R_FRONTEND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define R_FRONTEND_VERSION "0.1.0"
#define R_FRONTEND_CORE_REVISION "0.1.0-draft.105"

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
typedef struct RFrontendArtifactOptions {
    const char *entry;
    const char *profile;
    const uint8_t *target_manifest;
    size_t target_manifest_length;
    const uint8_t *link_manifest;
    size_t link_manifest_length;
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
RFrontendStatus r_frontend_set_profile(RFrontendContext *context, const char *profile);

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
 * Loads one ABI record document (schema r-abi-record-0.1) produced by
 * tools/generate_c_abi_record.py. A malformed document returns R_FRONTEND_INVALID_ARGUMENT.
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
/* Reachable async functions require a successful r_frontend_lower_mir call first. */
RFrontendStatus
r_frontend_emit_c17(const RFrontendContext *context, RFrontendWriteFn writer, void *user_data);
RFrontendStatus r_frontend_emit_c17_with_options(const RFrontendContext *context,
                                                 const RFrontendArtifactOptions *options,
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
 * Bridge translation unit of R-CMAP-0026: for every reserved-class import it includes the
 * verified headers and defines the private r_bridge_<identifier> forwarding function that the
 * generated program calls. It is linked with the program. Requires r_frontend_resolve_links.
 */
RFrontendStatus r_frontend_emit_c17_bridge(const RFrontendContext *context,
                                           const RFrontendArtifactOptions *options,
                                           RFrontendWriteFn writer,
                                           void *user_data);
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
