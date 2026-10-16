module runtime.types;

// The C types of the runtime headers that the units written in R share, proved against those
// headers (R-FFI-0041), and the C functions of the runtime and of the C library they call.
@link(name = "r.runtime", kind = "static")
@header("r_runtime_allocator.h")
@header("r_runtime_budget.h")
@header("r_runtime_core.h")
@header("r_runtime_0_1.h")
@header("r_runtime_task.h")
@header("r_runtime_own.h")
@header("r_runtime_array.h")
@header("r_runtime_string.h")
@header("r_runtime_list.h")
@header("r_runtime_dict.h")
@header("r_runtime_arc.h")
@header("r_runtime_rc.h")
@header("r_runtime_drop.h")
@header("r_runtime_darwin_event.h")
@abi("r-runtime-abi")
extern "C" {
    @repr(C)
    @c_type(name = "RRuntimePanicCategory", kind = "typedef")
    enum PanicCategory : c_uint {
        R_RUNTIME_PANIC_EXPLICIT = 1,
        R_RUNTIME_PANIC_BOUNDS = 2,
        R_RUNTIME_PANIC_INTEGER_OVERFLOW = 3,
        R_RUNTIME_PANIC_DIVISION_BY_ZERO = 4,
        R_RUNTIME_PANIC_INVALID_SHIFT = 5,
        R_RUNTIME_PANIC_INVALID_CONVERSION = 6,
        R_RUNTIME_PANIC_ALLOCATION_FAILURE = 7,
        R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW = 8,
        R_RUNTIME_PANIC_SCOPED_THREAD_PANIC = 9,
        R_RUNTIME_PANIC_ONCE_POISONED = 10,
        R_RUNTIME_PANIC_THREAD_LOCAL_LIFETIME = 11,
        R_RUNTIME_PANIC_STACK_EXHAUSTION = 12,
        R_RUNTIME_PANIC_CONTRACT_VIOLATION = 13,
    };

    @repr(C)
    @c_type(name = "RRuntimeSourceSpan", kind = "typedef")
    struct SourceSpan {
        @c_field(name = "module")
        c_uint32 module_id;
        c_uint32 start;
        c_uint32 end;
    };

    @repr(C)
    @c_type(name = "RRuntimePanicReportData", kind = "typedef")
    struct PanicReport {
        PanicCategory category;
        SourceSpan span;
        c_uint32 text_length;
        c_char[256] text;
    };

    // The tasks of the runtime (r_runtime_task.h): their records are the runtime's own
    // (runtime/r/task.r); C sees incomplete types.
    @c_type(name = "RRuntimeTask", kind = "typedef")
    opaque struct TaskHandle;
    @c_type(name = "RRuntimeTaskExecution", kind = "typedef")
    opaque struct ExecutionHandle;
    @c_type(name = "RRuntimeTaskExternalExecution", kind = "typedef")
    opaque struct ExternalHandle;

    @repr(C)
    @c_type(name = "RRuntimeTypeInfo", kind = "typedef")
    struct TypeInfo {
        c_size size;
        c_size alignment;
        raw fn?(raw void*?, raw void*?) -> void move_initialize;
        @c_field(name = "drop")
        raw fn?(raw void*?) -> void drop_value;
    };

    @repr(C)
    @c_type(name = "RRuntimeOwn", kind = "typedef")
    struct Own {
        raw void*? allocation;
        TypeInfo type;
        c_size allocation_alignment;
    };

    // A struct with atomic members passes through pointers only (R-FFI-0017).
    @repr(C)
    @c_type(name = "RRuntimeAllocator", kind = "typedef")
    struct Allocator {
        atomic c_uint64 attempt_count;
        atomic c_uint64 fail_at_attempt;
    };

    // The record of a budget is the runtime's own (runtime/r/allocator.r); C sees an incomplete
    // type.
    @c_type(name = "RRuntimeBudget", kind = "typedef")
    opaque struct Budget;

    @repr(C)
    @c_type(name = "RRuntimeAllocationStatus", kind = "typedef")
    enum AllocationStatus : c_uint {
        R_RUNTIME_ALLOCATION_OK = 0,
        R_RUNTIME_ALLOCATION_INVALID = 1,
        R_RUNTIME_ALLOCATION_SIZE_OVERFLOW = 2,
        R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT = 3,
        R_RUNTIME_ALLOCATION_EXHAUSTED = 4,
    };

    @repr(C)
    @c_type(name = "RRuntimeOwnStatus", kind = "typedef")
    enum OwnStatus : c_uint {
        R_RUNTIME_OWN_OK = 0,
        R_RUNTIME_OWN_INVALID = 1,
        R_RUNTIME_OWN_ALLOCATION_FAILED = 2,
        R_RUNTIME_OWN_SIZE_OVERFLOW = 3,
        R_RUNTIME_OWN_UNSUPPORTED_ALIGNMENT = 4,
    };

    @repr(C)
    @c_type(name = "RRuntimeArray", kind = "typedef")
    struct Array {
        raw Allocator*? allocator;
        TypeInfo element;
        raw void*? data;
        c_size length;
        c_size capacity;
    };

    @repr(C)
    @c_type(name = "RRuntimeArrayStatus", kind = "typedef")
    enum ArrayStatus : c_uint {
        R_RUNTIME_ARRAY_OK = 0,
        R_RUNTIME_ARRAY_INVALID = 1,
        R_RUNTIME_ARRAY_ALLOCATION_FAILED = 2,
        R_RUNTIME_ARRAY_SIZE_OVERFLOW = 3,
        R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT = 4,
    };

    @repr(C)
    @c_type(name = "RRuntimeString", kind = "typedef")
    struct String {
        Array bytes;
    };

    @repr(C)
    @c_type(name = "RRuntimeStringStatus", kind = "typedef")
    enum StringStatus : c_uint {
        R_RUNTIME_STRING_OK = 0,
        R_RUNTIME_STRING_INVALID = 1,
        R_RUNTIME_STRING_ALLOCATION_FAILED = 2,
        R_RUNTIME_STRING_SIZE_OVERFLOW = 3,
        R_RUNTIME_STRING_UNSUPPORTED_ALIGNMENT = 4,
        R_RUNTIME_STRING_INVALID_UTF8 = 5,
        R_RUNTIME_STRING_OUT_OF_BOUNDS = 6,
        R_RUNTIME_STRING_NOT_SCALAR_BOUNDARY = 7,
    };

    @repr(C)
    @c_type(name = "RRuntimeListNode", kind = "typedef")
    struct ListNode {
        raw ListNode*? previous;
        raw ListNode*? next;
    };

    @repr(C)
    @c_type(name = "RRuntimeList", kind = "typedef")
    struct List {
        raw Allocator*? allocator;
        TypeInfo element;
        raw ListNode*? first;
        raw ListNode*? last;
        c_size length;
        c_size value_offset;
        c_size node_alignment;
    };

    @repr(C)
    @c_type(name = "RRuntimeListIterator", kind = "typedef")
    struct ListIterator {
        @c_field(name = "list")
        raw const List*? list_value;
        raw const ListNode*? next;
    };

    @repr(C)
    @c_type(name = "RRuntimeListStatus", kind = "typedef")
    enum ListStatus : c_uint {
        R_RUNTIME_LIST_OK = 0,
        R_RUNTIME_LIST_INVALID = 1,
        R_RUNTIME_LIST_ALLOCATION_FAILED = 2,
        R_RUNTIME_LIST_SIZE_OVERFLOW = 3,
        R_RUNTIME_LIST_UNSUPPORTED_ALIGNMENT = 4,
    };

    @repr(C)
    @c_type(name = "RRuntimeDictKeyInfo", kind = "typedef")
    struct DictKeyInfo {
        TypeInfo type;
        raw fn?(raw const void*?) -> c_uint64 hash;
        raw fn?(raw const void*?, raw const void*?) -> c_bool equal;
    };

    @repr(C)
    @c_type(name = "RRuntimeDictStatus", kind = "typedef")
    enum DictStatus : c_uint {
        R_RUNTIME_DICT_OK = 0,
        R_RUNTIME_DICT_INVALID = 1,
        R_RUNTIME_DICT_ALLOCATION_FAILED = 2,
        R_RUNTIME_DICT_SIZE_OVERFLOW = 3,
        R_RUNTIME_DICT_UNSUPPORTED_ALIGNMENT = 4,
    };

    @repr(C)
    @c_type(name = "RRuntimeDict", kind = "typedef")
    struct Dict {
        raw Allocator*? allocator;
        DictKeyInfo key;
        TypeInfo value;
        raw void*? slots;
        raw void*? entries;
        c_size length;
        c_size tombstones;
        c_size capacity;
        c_size entry_capacity;
        c_size entries_offset;
        c_size first_order;
        c_size last_order;
        c_size key_offset;
        c_size value_offset;
        c_size slot_size;
        c_size slot_alignment;
        c_size entry_size;
        c_size entry_alignment;
        c_uint64 seed;
    };

    @repr(C)
    @c_type(name = "RRuntimeDictSlotState", kind = "typedef")
    enum DictSlotState : c_uint {
        R_RUNTIME_DICT_SLOT_EMPTY = 0,
        R_RUNTIME_DICT_SLOT_LIVE = 1,
        R_RUNTIME_DICT_SLOT_TOMBSTONE = 2,
    };

    @repr(C)
    @c_type(name = "RRuntimeDictIndexSlot", kind = "typedef")
    struct DictIndexSlot {
        c_size entry_index;
        DictSlotState state;
    };

    @repr(C)
    @c_type(name = "RRuntimeDictEntryHeader", kind = "typedef")
    struct DictEntryHeader {
        c_uint64 hash;
    };

    @repr(C)
    @c_type(name = "RRuntimeDictIterator", kind = "typedef")
    struct DictIterator {
        @c_field(name = "dict")
        raw const Dict*? dict_value;
        c_size next_order;
    };

    @repr(C)
    @c_type(name = "RRuntimeDictEntryRef", kind = "typedef")
    struct DictEntryRef {
        raw const void*? key;
        raw const void*? value;
    };

    // The control blocks of shared owners are the runtime's own (runtime/r/arc.r, rc.r); C sees
    // them as incomplete types.
    @c_type(name = "RRuntimeArcControl", kind = "struct")
    opaque struct ArcControlHandle;

    @repr(C)
    @c_type(name = "RRuntimeArc", kind = "typedef")
    struct Arc {
        raw ArcControlHandle*? control;
    };

    @repr(C)
    @c_type(name = "RRuntimeWeakArc", kind = "typedef")
    struct WeakArc {
        raw ArcControlHandle*? control;
    };

    @repr(C)
    @c_type(name = "RRuntimeArcStatus", kind = "typedef")
    enum ArcStatus : c_uint {
        R_RUNTIME_ARC_OK = 0,
        R_RUNTIME_ARC_INVALID = 1,
        R_RUNTIME_ARC_ALLOCATION_FAILED = 2,
        R_RUNTIME_ARC_SIZE_OVERFLOW = 3,
        R_RUNTIME_ARC_UNSUPPORTED_ALIGNMENT = 4,
        R_RUNTIME_ARC_COUNT_OVERFLOW = 5,
        R_RUNTIME_ARC_EXPIRED = 6,
        R_RUNTIME_ARC_NOT_UNIQUE = 7,
    };

    @c_type(name = "RRuntimeRcControl", kind = "struct")
    opaque struct RcControlHandle;

    @repr(C)
    @c_type(name = "RRuntimeRc", kind = "typedef")
    struct Rc {
        raw RcControlHandle*? control;
    };

    @repr(C)
    @c_type(name = "RRuntimeWeakRc", kind = "typedef")
    struct WeakRc {
        raw RcControlHandle*? control;
    };

    @repr(C)
    @c_type(name = "RRuntimeRcStatus", kind = "typedef")
    enum RcStatus : c_uint {
        R_RUNTIME_RC_OK = 0,
        R_RUNTIME_RC_INVALID = 1,
        R_RUNTIME_RC_ALLOCATION_FAILED = 2,
        R_RUNTIME_RC_SIZE_OVERFLOW = 3,
        R_RUNTIME_RC_UNSUPPORTED_ALIGNMENT = 4,
        R_RUNTIME_RC_COUNT_OVERFLOW = 5,
        R_RUNTIME_RC_EXPIRED = 6,
        R_RUNTIME_RC_NOT_UNIQUE = 7,
    };

    @repr(C)
    @c_type(name = "RRuntimeDropMemberKind", kind = "typedef")
    enum DropMemberKind : c_uint {
        R_RUNTIME_DROP_MEMBER_NONE = 0,
        R_RUNTIME_DROP_MEMBER_LEAF = 1,
        R_RUNTIME_DROP_MEMBER_OWN = 2,
        R_RUNTIME_DROP_MEMBER_RC = 3,
        R_RUNTIME_DROP_MEMBER_ARC = 4,
        R_RUNTIME_DROP_MEMBER_ARRAY = 5,
        R_RUNTIME_DROP_MEMBER_LIST = 6,
        R_RUNTIME_DROP_MEMBER_DICT = 7,
    };

    @repr(C)
    @c_type(name = "RRuntimeDropMember", kind = "typedef")
    struct DropMember {
        DropMemberKind kind;
        raw void*? address;
        @c_field(name = "drop")
        raw fn?(raw void*?) -> void drop_value;
        raw const DropNode*? child;
    };

    @repr(C)
    @c_type(name = "RRuntimeDropNode", kind = "struct")
    struct DropNode {
        raw fn?(raw void*?, c_size, raw DropMember*?) -> c_bool cursor;
        c_size size;
        c_size alignment;
    };

    // The process-wide sequence of the darwin adapters' terminal events (runtime/darwin/source).
    @safety("R-RUNTIME-EVENT-SEQUENCE-NEXT", "None")
    c_uint64 r_runtime_darwin_event_sequence_next();

    @c_constant(name = "R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE")
    const c_size maximum_object_size = 2305843009213693951u64 as c_size;
    @c_constant(name = "R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT")
    const c_size maximum_alignment = 1073741824u64 as c_size;
}

@link(name = "system.libc", kind = "system")
@header("string.h")
extern "C" {
    @safety("R-RUNTIME-MEMCPY", "both ranges are valid for size bytes and do not overlap")
    raw void*? memcpy(raw void*? destination, raw const void*? source, c_size size);
    @safety("R-RUNTIME-MEMSET", "the range is valid for size bytes")
    raw void*? memset(raw void*? destination, c_int value, c_size size);
}

// The truth of a C _Bool, and a C _Bool of a truth.
bool is_true(c_bool value) {
    return value != 0i32 as c_bool;
}

c_bool c_bool_of(bool value) {
    if (value == true) {
        return 1i32 as c_bool;
    }
    return 0i32 as c_bool;
}

// The largest usize, the bound the C arithmetic of the runtime checks against (SIZE_MAX).
const usize size_maximum = 18446744073709551615usize;

// `value` rounded up to `alignment`, a power of two, or false when that overflows.
bool align_up(usize value, usize alignment, usize* result) {
    usize mask = alignment - 1usize;
    if (value > size_maximum - mask) {
        return false;
    }
    *result = (value + mask) & (size_maximum ^ mask);
    return true;
}

// Whether a compare-and-exchange took place, with the value it observed.
bool exchanged(core::atomic_compare_exchange_result<usize> result, usize* observed) {
    switch (move result) {
    case variant core::atomic_compare_exchange_result::exchanged(move value):
        *observed = value;
        return true;
    case variant core::atomic_compare_exchange_result::unchanged(move value):
        *observed = value;
        return false;
    }
}

// Moves one value of `type` into uninitialized storage: through the type's move glue, or as its
// bytes when it has none.
void move_value(TypeInfo type, raw void*? destination, raw void*? source) {
    unsafe {
        raw fn?(raw void*?, raw void*?) -> void glue = type.move_initialize;
        if (glue != null) {
            glue(destination, source);
            return;
        }
        memcpy(destination, source as raw const void*?, type.size) as void;
    }
}
