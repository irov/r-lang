#ifndef PROBE_LIBRARY_H
#define PROBE_LIBRARY_H

#include <stddef.h>
#include <wchar.h>

/* Minimal C17 provider for the extern "C" import conformance fixtures. */
int probe_increment(int value);
int probe_apply(int (*callback)(int), int value);
const char *probe_name(void);
size_t probe_text_length(const char *text);
void probe_store(int *destination, int value);

/* Opaque handle behind a struct tag (R-FFI-0015, @c_type kind "struct"). */
struct probe_handle;
struct probe_handle *probe_open(int seed);
int probe_read(const struct probe_handle *handle);
void probe_close(struct probe_handle *handle);

/* Opaque handle behind a typedef (R-FFI-0016, @c_type kind "typedef"). */
typedef struct probe_session_s probe_session;
probe_session *probe_session_open(int id);
int probe_session_id(const probe_session *session);
void probe_session_close(probe_session *session);

/* Verified constants (R-FFI-0018): macro, negative macro, unsigned macro, enumerator. */
#define PROBE_OK 0
#define PROBE_NEGATIVE (-7)
#define PROBE_LIMIT 4000000000U
enum probe_status {
    PROBE_STATUS_READY = 3
};

/* Imported objects (R-FFI-0022): mutable, const and thread-local. */
extern int probe_counter;
extern const int probe_version;
extern _Thread_local int probe_thread_slot;

/* Variadic import (R-FFI-0005): sums count int arguments. */
int probe_sum(int count, ...);

/* Complete aggregates with verified member inventories (R-FFI-0017). */
struct probe_pair {
    int left;
    int right;
};
typedef struct probe_rect_s {
    struct probe_pair origin;
    unsigned char flag;
    double scale;
} probe_rect;
enum probe_mode {
    PROBE_MODE_IDLE = 0,
    PROBE_MODE_RUN = 2,
    PROBE_MODE_HALT = -1
};
extern struct probe_pair probe_origin;
int probe_pair_sum(struct probe_pair pair);
struct probe_pair probe_pair_make(int left, int right);
double probe_rect_area(probe_rect rect);
probe_rect probe_rect_scale(probe_rect rect, double factor);
enum probe_status probe_status_next(enum probe_status status);
enum probe_mode probe_mode_after(enum probe_mode mode);

/* Structs that R declares itself and passes to imports: each is proven against the C type
   at its position in the prototype (R-FFI-0021, R-FFI-0041). */
struct probe_point {
    int x;
    int y;
};
typedef struct {
    struct probe_point corner;
    unsigned char tag[4];
    enum probe_mode mode;
    const char *label;
} probe_marker;
struct probe_node {
    int value;
    struct probe_node *next;
};
int probe_point_sum(struct probe_point point);
struct probe_point probe_point_make(int x, int y);
void probe_point_scale(struct probe_point *point, int factor);
int probe_point_apply(int (*visit)(struct probe_point), struct probe_point point);
probe_marker probe_marker_shift(probe_marker marker, int dx);
probe_marker probe_marker_raw(int mode, int labeled);
int probe_marker_visit(int (*visit)(probe_marker), int mode);
int probe_node_total(const struct probe_node *head);
enum probe_mode probe_mode_raw(int value);

/* Objects of such structs and a reserved-name object reached through the accessor bridge
   (R-FFI-0022, R-FFI-0057), and an enum object whose representation C may corrupt. */
extern struct probe_point probe_corner;
extern struct probe_node *probe_chain;
extern int total_count;
extern enum probe_mode probe_current_mode;
void probe_mode_corrupt(int value);

/* Functions R imports with a never result (R-FUNC-0003): one ends the process, one breaks the
   contract by returning, and one calls a never callback. */
_Noreturn void probe_exit(int status);
void probe_returns(void);
int probe_call_fatal(void (*fatal)(int), int code);

/* A member hidden in padding: layout alone cannot tell it apart (R-CONF-G009). */
struct probe_padded {
    int left;
    char hidden;
    double scale;
};
double probe_padded_scale(struct probe_padded padded);

/* Retained userdata through managed-token adapters (R-FFI-0060, R-CMAP-0037). */
typedef const void *(*probe_retain_fn)(const void *token);
typedef void (*probe_release_fn)(const void *token);
void probe_userdata_store(const void *token, probe_retain_fn retain, probe_release_fn release);
int probe_userdata_share(void);
int probe_userdata_count(void);
void probe_userdata_clear(void);

/* Floating environment observation across the C boundary (R-IDB-020). */
int probe_rounding_is_default(void);
int probe_flags_raised(void);
void probe_set_rounding_upward(void);
void probe_raise_division_by_zero(void);
int probe_call_with_upward(int (*callback)(int), int value);

/* C-origin entries: nested re-entry, a fresh C thread, null and invalid-enum ingress. */
int probe_call_on_thread(int (*callback)(int), int value);
int probe_call_null(int (*callback)(int *));
int probe_call_mode(int (*callback)(enum probe_mode), int raw_value);
wint_t probe_wint_echo(wint_t value);

#endif
