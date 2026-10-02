#ifndef R_STD_ERROR_H
#define R_STD_ERROR_H

#include "r_std_error_types.h"

#include "r_std_async.h"
#include "r_std_bytes.h"
#include "r_std_convert.h"
#include "r_std_format.h"
#include "r_std_fs.h"
#include "r_std_net.h"
#include "r_std_string.h"
#include "r_std_sync.h"
#include "r_std_thread.h"
#include "r_std_time.h"

typedef RStdStringAllocValueResult RStdErrorDiagnosticResult;

/* Returns a program-lifetime UTF-8 view and never allocates or panics. */
RStdStringView r_std_error_name(RStdError value);

/*
 * Ownership: allocator is a shared runtime reference retained by the successful string. The
 * result owns its diagnostic storage. Allocation failure exposes no partial string.
 */
RStdErrorDiagnosticResult r_std_error_diagnostic(RRuntimeAllocator *allocator, RStdError value);

/* Typed errors are copied; detail fields remain available in the caller's source value. */
RStdError r_std_error_from_alloc(RStdAllocError value);
RStdError r_std_error_from_async(RStdAsyncStartError value);
RStdError r_std_error_from_bytes(RStdBytesError value);
RStdError r_std_error_from_string(RStdStringError value);
RStdError r_std_error_from_boundary(RStdStringBoundaryError value);
RStdError r_std_error_from_duration(RStdTimeDurationError value);
RStdError r_std_error_from_path(RStdFsPathError value);
RStdError r_std_error_from_address(RStdNetAddressError value);
RStdError r_std_error_from_barrier(RStdSyncBarrierError value);
RStdError r_std_error_from_thread(RStdThreadError value);
RStdError r_std_error_from_format(RStdFormatError value);
RStdError r_std_error_from_parse(RStdConvertParseError value);
RStdError r_std_error_from_range(RStdConvertRangeError value);

#endif
