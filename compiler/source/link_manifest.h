#ifndef R_LINK_MANIFEST_H
#define R_LINK_MANIFEST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Link kinds of R-FFI-0029. UNSPECIFIED means the source or manifest omitted the kind. */
typedef enum RLinkKind {
    R_LINK_KIND_UNSPECIFIED = 0,
    R_LINK_KIND_STATIC,
    R_LINK_KIND_DYNAMIC,
    R_LINK_KIND_FRAMEWORK,
    R_LINK_KIND_SYSTEM
} RLinkKind;

typedef enum RLinkSymbolKind {
    R_LINK_SYMBOL_FUNCTION = 0,
    R_LINK_SYMBOL_DATA,
    R_LINK_SYMBOL_TLS
} RLinkSymbolKind;

typedef struct RLinkManifestEntryView {
    const uint8_t *logical_name;
    size_t logical_name_length;
    bool available;
    RLinkKind kind;
    bool implicit_c_runtime;
} RLinkManifestEntryView;

/* One R-FFI-0031 inventory entry. Views borrow parser storage during the callback only. */
typedef struct RLinkManifestSymbolView {
    const uint8_t *c_identifier;
    size_t c_identifier_length;
    RLinkSymbolKind kind;
    bool weak;
} RLinkManifestSymbolView;

typedef bool (*RLinkManifestVisitFn)(void *user_data, RLinkManifestEntryView entry);
typedef bool (*RLinkManifestSymbolVisitFn)(void *user_data,
                                           const RLinkManifestEntryView *entry,
                                           RLinkManifestSymbolView symbol);
/* One feature_test_definitions string: NAME or NAME=VALUE with a C identifier NAME. */
typedef bool (*RLinkManifestDefinitionVisitFn)(void *user_data,
                                               const RLinkManifestEntryView *entry,
                                               const uint8_t *definition,
                                               size_t definition_length);

typedef struct RLinkManifestVisitors {
    RLinkManifestVisitFn entry;
    RLinkManifestSymbolVisitFn symbol;
    RLinkManifestDefinitionVisitFn definition;
} RLinkManifestVisitors;

typedef enum RLinkManifestVisitStatus {
    R_LINK_MANIFEST_VISIT_OK = 0,
    R_LINK_MANIFEST_VISIT_INVALID,
    R_LINK_MANIFEST_VISIT_REJECTED
} RLinkManifestVisitStatus;

RLinkManifestVisitStatus r_link_manifest_visit(const uint8_t *bytes,
                                               size_t length,
                                               RLinkManifestVisitFn visitor,
                                               void *user_data,
                                               size_t *entry_count);

/*
 * Visits every entry in manifest order. After an accepted entry, every object of its symbols
 * array is delivered to symbol_visitor with that entry. A NULL symbol visitor skips the array.
 */
RLinkManifestVisitStatus
r_link_manifest_visit_with_symbols(const uint8_t *bytes,
                                   size_t length,
                                   RLinkManifestVisitFn visitor,
                                   RLinkManifestSymbolVisitFn symbol_visitor,
                                   void *user_data,
                                   size_t *entry_count);

/*
 * Visits entries, then each accepted entry's symbols and feature_test_definitions arrays in
 * manifest order. Any visitor may be NULL; both arrays are skipped when their visitors are NULL.
 */
RLinkManifestVisitStatus r_link_manifest_visit_full(const uint8_t *bytes,
                                                    size_t length,
                                                    const RLinkManifestVisitors *visitors,
                                                    void *user_data,
                                                    size_t *entry_count);

/* R-FFI-0028 logical link name syntax. */
bool r_link_manifest_logical_name_is_valid(const uint8_t *bytes, size_t length);
/* The bytes of a valid manifest between the brackets of its links array, without the white
   space next to them; false for an invalid manifest. */
bool r_link_manifest_links_span(const uint8_t *bytes, size_t length, size_t *first, size_t *last);
bool r_link_manifest_kind_from_text(const uint8_t *bytes, size_t length, RLinkKind *kind);
const char *r_link_manifest_kind_name(RLinkKind kind);

#endif
