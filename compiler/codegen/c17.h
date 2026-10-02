#ifndef R_CODEGEN_C17_H
#define R_CODEGEN_C17_H

#include "r_frontend.h"

#ifdef __cplusplus
extern "C" {
#endif

RFrontendStatus
r_frontend_emit_c17(const RFrontendContext *context, RFrontendWriteFn writer, void *user_data);
RFrontendStatus r_frontend_emit_c17_with_options(const RFrontendContext *context,
                                                 const RFrontendArtifactOptions *options,
                                                 RFrontendWriteFn writer,
                                                 void *user_data);

#ifdef __cplusplus
}
#endif

#endif
