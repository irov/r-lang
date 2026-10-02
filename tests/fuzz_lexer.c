#include "r_frontend.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    RFrontendContext *context = r_frontend_create(NULL);
    if (context != NULL) {
        RSourceId source_id = R_SOURCE_ID_INVALID;
        if (r_frontend_add_source(context, "fuzz.r", data, size, &source_id) == R_FRONTEND_OK) {
            (void)r_frontend_lex(context, source_id);
        }
        r_frontend_destroy(context);
    }
    return 0;
}
