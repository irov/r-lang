#include "r_library_json_internal.h"

static uint64_t r_json_string_hash(const void *value) {
    RStdJsonByteView text = r_json_string_view(value);
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0U; i < text.length; ++i)
        hash = (hash ^ text.data[i]) * UINT64_C(1099511628211);
    return hash;
}

static bool r_json_string_equal(const void *left, const void *right) {
    return r_json_view_equal(r_json_string_view(left), r_json_string_view(right));
}

static void r_json_string_move(void *destination, void *source) {
    *(RStdString *)destination = *(RStdString *)source;
    *(RStdString *)source = (RStdString){0};
}

static void r_json_string_drop(void *value) {
    r_runtime_string_destroy(value);
}

bool r_json_cursor_list_push(RStdJsonCursor *cursor, RRuntimeList *list, void *value) {
    void *stored;
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    RRuntimeListStatus status = r_runtime_list_push_back(list, value, &stored);
    switch (status) {
    case R_RUNTIME_LIST_OK:
        return true;
    case R_RUNTIME_LIST_SIZE_OVERFLOW:
        cursor->outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_SIZE_OVERFLOW);
        break;
    case R_RUNTIME_LIST_UNSUPPORTED_ALIGNMENT:
        cursor->outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT);
        break;
    case R_RUNTIME_LIST_INVALID:
        cursor->outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_INVALID);
        break;
    default:
        cursor->outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_EXHAUSTED);
        break;
    }
    return false;
}

void r_json_cursor_dict_initialize(RStdJsonCursor *cursor,
                                   RRuntimeDict *dict,
                                   RRuntimeTypeInfo value) {
    RRuntimeDictKeyInfo key = {
        .type = {sizeof(RStdString), _Alignof(RStdString), r_json_string_move, r_json_string_drop},
        .hash = r_json_string_hash,
        .equal = r_json_string_equal,
    };
    (void)r_runtime_dict_initialize(dict, cursor->allocator, key, value, UINT64_C(0));
}

bool r_json_cursor_dict_insert(RStdJsonCursor *cursor,
                               RRuntimeDict *dict,
                               RStdString *key,
                               void *value) {
    bool replaced;
    if (cursor->outcome.status != R_STD_JSON_CALL_SUCCESS)
        return false;
    if (r_runtime_dict_contains(dict, key))
        return r_json_cursor_fail(cursor, R_STD_JSON_ERROR_DUPLICATE_KEY);
    RRuntimeDictStatus status = r_runtime_dict_insert(dict, key, value, NULL, &replaced);
    switch (status) {
    case R_RUNTIME_DICT_OK:
        return true;
    case R_RUNTIME_DICT_SIZE_OVERFLOW:
        cursor->outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_SIZE_OVERFLOW);
        break;
    case R_RUNTIME_DICT_UNSUPPORTED_ALIGNMENT:
        cursor->outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT);
        break;
    case R_RUNTIME_DICT_INVALID:
        cursor->outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_INVALID);
        break;
    default:
        cursor->outcome = r_json_allocation_result(R_RUNTIME_ALLOCATION_EXHAUSTED);
        break;
    }
    return false;
}
