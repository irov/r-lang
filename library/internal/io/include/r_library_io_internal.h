#ifndef R_LIBRARY_IO_INTERNAL_H
#define R_LIBRARY_IO_INTERNAL_H

#include "r_std_io.h"

RStdIoTaskStartResult r_library_internal_io_read(const RStdIoInput *stream,
                                                 RRuntimeArray *buffer,
                                                 RStdIoDeadline deadline);
RStdIoTaskStartResult r_library_internal_io_write(const RStdIoOutput *stream,
                                                  RRuntimeArray *buffer,
                                                  RStdIoDeadline deadline);
RStdIoTaskStartResult r_library_internal_io_write_all(const RStdIoOutput *stream,
                                                      RRuntimeArray *buffer,
                                                      RStdIoDeadline deadline);
RStdIoTaskStartResult r_library_internal_io_read_into(const RStdIoInput *stream,
                                                      RStdIoMutableBytes target,
                                                      RStdIoDeadline deadline);
RStdIoTaskStartResult r_library_internal_io_write_from(const RStdIoOutput *stream,
                                                       RStdIoConstBytes source,
                                                       RStdIoDeadline deadline);
RStdIoTaskStartResult r_library_internal_io_write_all_from(const RStdIoOutput *stream,
                                                           RStdIoConstBytes source,
                                                           RStdIoDeadline deadline);
RStdIoTaskStartResult r_library_internal_io_write_shared(const RStdIoOutput *stream,
                                                         RRuntimeArc *buffer,
                                                         size_t offset,
                                                         size_t length,
                                                         RStdIoDeadline deadline);
RStdIoTaskStartResult r_library_internal_io_flush(const RStdIoOutput *stream,
                                                  RStdIoDeadline deadline);
RStdIoInput r_library_internal_io_stdin(void);
RStdIoOutput r_library_internal_io_stdout(void);
RStdIoOutput r_library_internal_io_stderr(void);
RStdIoTaskStartResult r_library_internal_io_close_input(RStdIoInput *stream,
                                                        RStdIoDeadline deadline);
RStdIoTaskStartResult r_library_internal_io_close_output(RStdIoOutput *stream,
                                                         RStdIoDeadline deadline);

#endif
