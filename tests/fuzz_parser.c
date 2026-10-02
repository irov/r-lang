#include "r_frontend.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    RFrontendContext *context = r_frontend_create(NULL);
    if (context != NULL) {
        RSourceId source_id = R_SOURCE_ID_INVALID;
        if (r_frontend_add_source(context, "fuzz.r", data, size, &source_id) == R_FRONTEND_OK) {
            RSyntaxNodeId cst_root = R_SYNTAX_NODE_ID_INVALID;
            RAstNodeId ast_root = R_AST_NODE_ID_INVALID;
            (void)r_frontend_parse_cst(context, source_id, &cst_root);
            (void)r_frontend_lower_ast(context, source_id, &ast_root);
        }
        r_frontend_destroy(context);
    }
    return 0;
}
