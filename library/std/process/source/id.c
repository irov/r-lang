#include "r_std_process.h"

uint64_t r_std_process_id(const RStdProcessChild *child) {
    return child->identity;
}
