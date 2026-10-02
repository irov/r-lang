#include "frontend_internal.h"

static const RStandardMathOperationDescriptor r_standard_math_operations[] = {
#define R_STANDARD_MATH_OPERATION(name,                                                            \
                                  c_symbol,                                                        \
                                  result_type,                                                     \
                                  parameter0_type,                                                 \
                                  parameter1_type,                                                 \
                                  parameter_count,                                                 \
                                  checked,                                                         \
                                  rule_id)                                                         \
    {name,                                                                                         \
     c_symbol,                                                                                     \
     result_type,                                                                                  \
     parameter0_type,                                                                              \
     parameter1_type,                                                                              \
     rule_id,                                                                                      \
     UINT32_C(parameter_count),                                                                    \
     checked},
#include "standard_math_operations.generated.inc"
#undef R_STANDARD_MATH_OPERATION
};

size_t r_standard_math_operation_count(void) {
    return sizeof(r_standard_math_operations) / sizeof(r_standard_math_operations[0]);
}

const RStandardMathOperationDescriptor *r_standard_math_operation(uint64_t operation_id) {
    if ((operation_id == UINT64_C(0)) ||
        (operation_id > (uint64_t)r_standard_math_operation_count())) {
        return NULL;
    }
    return &r_standard_math_operations[(size_t)operation_id - 1U];
}
