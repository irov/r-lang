#include "r_library_io_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_io.h"

typedef enum RLibraryIoConsoleStream {
    R_LIBRARY_IO_CONSOLE_INPUT = 0,
    R_LIBRARY_IO_CONSOLE_OUTPUT,
    R_LIBRARY_IO_CONSOLE_ERROR
} RLibraryIoConsoleStream;

static RRuntimeDarwinIoHandle *retain_console(RLibraryIoConsoleStream stream) {
    RRuntimeDarwinIoHandle *handle = NULL;

    switch (stream) {
    case R_LIBRARY_IO_CONSOLE_INPUT:
        handle = r_runtime_darwin_io_process_stdin_retain();
        break;
    case R_LIBRARY_IO_CONSOLE_OUTPUT:
        handle = r_runtime_darwin_io_process_stdout_retain();
        break;
    case R_LIBRARY_IO_CONSOLE_ERROR:
        handle = r_runtime_darwin_io_process_stderr_retain();
        break;
    }
    if (handle == NULL) {
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    return handle;
}

RStdIoInput r_library_internal_io_stdin(void) {
    return (RStdIoInput){retain_console(R_LIBRARY_IO_CONSOLE_INPUT)};
}

RStdIoOutput r_library_internal_io_stdout(void) {
    return (RStdIoOutput){retain_console(R_LIBRARY_IO_CONSOLE_OUTPUT)};
}

RStdIoOutput r_library_internal_io_stderr(void) {
    return (RStdIoOutput){retain_console(R_LIBRARY_IO_CONSOLE_ERROR)};
}
