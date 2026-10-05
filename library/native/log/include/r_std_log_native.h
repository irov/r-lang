#ifndef R_STD_LOG_NATIVE_H
#define R_STD_LOG_NATIVE_H

/* The native provider of std.log::panic_reports (Library R-SLIB-LOG-0005): the reports of panics
   that nothing observed (Core R-ERR-0009), which the panic sink of the runtime queues for the
   listener made last instead of writing its line. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Starts a listener with a queue of capacity reports, at least one, that takes the reports from
   the previous listener on; returns its generation, never 0, or 0 when the queue cannot be
   allocated. */
uint64_t r_std_log_native_listen(size_t capacity);

/* Takes the oldest report of the listener of generation: 1 with the category name, the text and
   the place of the report written to the outputs, 0 when none is queued or the listener is no
   longer the last one. category and text address 32 and 256 writable bytes. */
int32_t r_std_log_native_take(uint64_t generation,
                              uint8_t *category,
                              size_t *category_length,
                              uint8_t *text,
                              size_t *text_length,
                              uint32_t *module,
                              uint32_t *start,
                              uint32_t *end);

/* The reports that found the queue of the listener of generation full and were written as lines;
   0 when it is no longer the last one. */
uint64_t r_std_log_native_dropped(uint64_t generation);

/* Ends the listener of generation when it is the last one: the reports still queued and every
   later one are written as lines again. */
void r_std_log_native_stop(uint64_t generation);

#ifdef __cplusplus
}
#endif

#endif
