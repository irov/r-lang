#ifndef R_LIBRARY_JSON_INTERNAL_H
#define R_LIBRARY_JSON_INTERNAL_H

#include "r_std_json.h"

/* Internal entry points are shared by thin public operations and generated converters. */
typedef struct RJsonMember {
    RStdString key;
    RStdJsonValue value;
} RJsonMember;

struct RJsonNode {
    struct RJsonNode *drop_next;
    RStdJsonKind kind;
    RRuntimeAllocator *allocator;
    union {
        bool boolean;
        RStdString text;
        RRuntimeArray elements;
        RRuntimeArray members;
    } as;
};

RStdJsonResult r_json_allocation_result(RRuntimeAllocationStatus status);
RStdJsonResult r_json_array_result(RRuntimeArrayStatus status);
RStdJsonResult r_json_string_result(RRuntimeStringStatus status);
RStdJsonResult r_json_failure(RStdJsonErrorCode code, size_t offset);
RStdJsonByteView r_json_string_view(const RStdString *string);
bool r_json_view_equal(RStdJsonByteView a, RStdJsonByteView b);
RStdJsonResult
r_json_copy_text(RStdString *target, RRuntimeAllocator *allocator, RStdJsonByteView source);
RStdJsonValueResult r_json_string_value(RRuntimeAllocator *allocator, RStdJsonByteView value);
RStdJsonValueResult r_json_new_node(RRuntimeAllocator *allocator, RStdJsonKind kind);
RStdJsonResult r_json_append(RStdJsonValue *target, RStdJsonValue *value);
RStdJsonResult r_json_insert(RStdJsonValue *target, RStdJsonByteView key, RStdJsonValue *value);
RStdJsonValueResult r_json_take_index(RStdJsonValue *target, size_t index);
RStdJsonValueResult r_json_take_field(RStdJsonValue *target, RStdJsonByteView key);
RStdJsonValueResult
r_json_parse(RRuntimeAllocator *allocator, RStdJsonByteView source, RStdJsonOptions options);
RStdJsonStringResult
r_json_stringify(RRuntimeAllocator *allocator, const RStdJsonValue *value, RStdJsonOptions options);
RStdJsonNumberResult r_json_number(RRuntimeAllocator *allocator, RStdJsonByteView source);
bool r_json_number_is_zero(RStdJsonByteView source);
bool r_json_number_valid(RStdJsonByteView source);
uint32_t r_json_simple_fold(uint32_t scalar);
bool r_json_name_equal(RStdJsonByteView left, RStdJsonByteView right, bool ignore_case);

#endif
