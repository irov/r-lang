/* The runtime and library headers a C wrapper of a codegen test sees in place of the program
 * (R_TEST_PROGRAM_PRELUDE, tests/check_codegen_program.cmake): the program is an object of the
 * LLVM backend, so the wrapper reaches it only through `main`, its C exports and the runtime and
 * library entries it renames. */
#ifndef R_TEST_CODEGEN_PROGRAM_PRELUDE_H
#define R_TEST_CODEGEN_PROGRAM_PRELUDE_H

#include "r_core.h"
#include "r_runtime_0_1.h"
#include "r_runtime_arc.h"
#include "r_runtime_array.h"
#include "r_runtime_dict.h"
#include "r_runtime_list.h"
#include "r_runtime_own.h"
#include "r_runtime_target_abi.h"
#include "r_std_alloc.h"
#include "r_std_array.h"
#include "r_std_async.h"
#include "r_std_bits.h"
#include "r_std_bytes.h"
#include "r_std_c.h"
#include "r_std_convert.h"
#include "r_std_dict.h"
#include "r_std_env.h"
#include "r_std_error.h"
#include "r_std_error_types.h"
#include "r_std_format.h"
#include "r_std_fs.h"
#include "r_std_hash.h"
#include "r_std_io.h"
#include "r_std_json.h"
#include "r_std_json_reader.h"
#include "r_std_list.h"
#include "r_std_math.h"
#include "r_std_net.h"
#include "r_std_process.h"
#include "r_std_secret.h"
#include "r_std_signal.h"
#include "r_std_string.h"
#include "r_std_sync.h"
#include "r_std_thread.h"
#include "r_std_time.h"
#include "r_std_utf8.h"

#include <float.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

#endif
