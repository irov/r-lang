#ifndef R_RUNTIME_DROP_H
#define R_RUNTIME_DROP_H

#include "r_runtime_type.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Iterative destruction of self-nesting aggregates (Core R-FUNC-0004). An aggregate that reaches
 * itself through own, rc or arc members, container elements (array, list, dict values), fixed
 * arrays or options is destroyed by r_runtime_drop_iterative instead of recursive drop glue, so
 * the stack depth never follows the shape of the object graph.
 */
typedef enum RRuntimeDropMemberKind {
    /* Absent storage: an empty owner or option. Nothing to do. */
    R_RUNTIME_DROP_MEMBER_NONE = 0,
    /* Storage dropped synchronously through drop; its depth is fixed by the type, not the data. */
    R_RUNTIME_DROP_MEMBER_LEAF,
    /* An RRuntimeOwn slot whose pointee is described by child. */
    R_RUNTIME_DROP_MEMBER_OWN,
    /* An RRuntimeRc slot whose pointee is described by child. */
    R_RUNTIME_DROP_MEMBER_RC,
    /* An RRuntimeArc slot whose pointee is described by child. */
    R_RUNTIME_DROP_MEMBER_ARC,
    /* An RRuntimeArray whose elements are described by child; destroyed last to first. */
    R_RUNTIME_DROP_MEMBER_ARRAY,
    /* An RRuntimeList whose element values are described by child; destroyed back to front. */
    R_RUNTIME_DROP_MEMBER_LIST,
    /* An RRuntimeDict whose values are described by child; the runtime drops each key after its
       value, last entry first. */
    R_RUNTIME_DROP_MEMBER_DICT
} RRuntimeDropMemberKind;

typedef struct RRuntimeDropNode RRuntimeDropNode;

typedef struct RRuntimeDropMember {
    RRuntimeDropMemberKind kind;
    /* Address of the member storage: the value, the owner slot or the container header. */
    void *address;
    /* LEAF only: destroys the member storage in place. */
    RRuntimeDropFn drop;
    /* Owner slots and containers only: description of the pointee or element storage. */
    const RRuntimeDropNode *child;
} RRuntimeDropMember;

/*
 * Describes member `index` of the storage at `node` in destruction order: for an aggregate the
 * user drop hook first, then the members in reverse declaration order (for a tagged enum, the
 * payload of the active variant); for an owner or container exactly one member, the storage
 * itself. Returns 0 once every member has been described. The cursor is pure: it reads only
 * tags and presence flags and never modifies the storage.
 */
typedef _Bool (*RRuntimeDropCursorFn)(void *node, size_t index, RRuntimeDropMember *member);

struct RRuntimeDropNode {
    RRuntimeDropCursorFn cursor;
    /* Size and alignment of the described storage; the element stride and buffer alignment of a
       container whose elements it describes. */
    size_t size;
    size_t alignment;
};

/*
 * Destroys the storage at `root`, described by `node`, exactly as the recursive drop glue
 * would (hook first, members in reverse declaration order, pointees and elements before their
 * owner is released), without recursion that follows the data: every pointee and element is
 * entered iteratively and the exhausted owner slots and container headers carry the traversal
 * state in the size and alignment words of their type records.
 * Ownership: root storage stays owned by the caller and is not deallocated; every pointee and
 * element reached through owner and container members is destroyed and its allocation released.
 */
void r_runtime_drop_iterative(void *root, const RRuntimeDropNode *node);

#ifdef __cplusplus
}
#endif

#endif
