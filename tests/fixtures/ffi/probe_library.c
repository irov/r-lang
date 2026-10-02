#include "probe_library.h"

#include <fenv.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

struct probe_handle {
    int value;
};

struct probe_session_s {
    int id;
};

int probe_counter = 0;
const int probe_version = 21;
_Thread_local int probe_thread_slot = 0;

int probe_increment(int value) {
    return value + 1;
}

int probe_apply(int (*callback)(int), int value) {
    return callback(value);
}

const char *probe_name(void) {
    return "probe";
}

size_t probe_text_length(const char *text) {
    return strlen(text);
}

void probe_store(int *destination, int value) {
    *destination = value;
}

struct probe_handle *probe_open(int seed) {
    struct probe_handle *handle = malloc(sizeof(*handle));

    if (handle != NULL) {
        handle->value = seed;
    }
    return handle;
}

int probe_read(const struct probe_handle *handle) {
    return handle->value;
}

void probe_close(struct probe_handle *handle) {
    free(handle);
}

probe_session *probe_session_open(int id) {
    probe_session *session = malloc(sizeof(*session));

    if (session != NULL) {
        session->id = id;
    }
    return session;
}

int probe_session_id(const probe_session *session) {
    return session->id;
}

void probe_session_close(probe_session *session) {
    free(session);
}

struct probe_pair probe_origin = {4, 5};

int probe_pair_sum(struct probe_pair pair) {
    return pair.left + pair.right;
}

struct probe_pair probe_pair_make(int left, int right) {
    struct probe_pair pair;

    pair.left = left;
    pair.right = right;
    return pair;
}

double probe_rect_area(probe_rect rect) {
    return (double)rect.origin.left * (double)rect.origin.right * rect.scale;
}

probe_rect probe_rect_scale(probe_rect rect, double factor) {
    rect.scale *= factor;
    rect.flag = 1;
    return rect;
}

enum probe_status probe_status_next(enum probe_status status) {
    return status;
}

enum probe_mode probe_mode_after(enum probe_mode mode) {
    return mode == PROBE_MODE_RUN ? PROBE_MODE_HALT : PROBE_MODE_RUN;
}

int probe_point_sum(struct probe_point point) {
    return point.x + point.y;
}

struct probe_point probe_point_make(int x, int y) {
    struct probe_point point;

    point.x = x;
    point.y = y;
    return point;
}

void probe_point_scale(struct probe_point *point, int factor) {
    point->x *= factor;
    point->y *= factor;
}

int probe_point_apply(int (*visit)(struct probe_point), struct probe_point point) {
    return visit(point);
}

probe_marker probe_marker_shift(probe_marker marker, int dx) {
    marker.corner.x += dx;
    marker.tag[3] = (unsigned char)(marker.tag[0] + marker.tag[1] + marker.tag[2]);
    marker.mode = marker.mode == PROBE_MODE_RUN ? PROBE_MODE_HALT : PROBE_MODE_RUN;
    return marker;
}

probe_marker probe_marker_raw(int mode, int labeled) {
    probe_marker marker;

    (void)memset(&marker, 0, sizeof(marker));
    /* A value outside the enumeration or a null label violates the R declaration. */
    marker.mode = (enum probe_mode)mode;
    marker.label = labeled != 0 ? "raw" : NULL;
    return marker;
}

int probe_marker_visit(int (*visit)(probe_marker), int mode) {
    /* The callback receives the raw mode: the C-origin entry validates the member. */
    return visit(probe_marker_raw(mode, 1));
}

int probe_node_total(const struct probe_node *head) {
    int total = 0;

    for (; head != NULL; head = head->next) {
        total += head->value;
    }
    return total;
}

enum probe_mode probe_mode_raw(int value) {
    return (enum probe_mode)value;
}

struct probe_point probe_corner = {7, 9};
static struct probe_node probe_chain_tail = {30, NULL};
static struct probe_node probe_chain_head = {12, &probe_chain_tail};
struct probe_node *probe_chain = &probe_chain_head;
int total_count = 0;
enum probe_mode probe_current_mode = PROBE_MODE_IDLE;

void probe_mode_corrupt(int value) {
    probe_current_mode = (enum probe_mode)value;
}

_Noreturn void probe_exit(int status) {
    exit(status);
}

void probe_returns(void) {
}

int probe_call_fatal(void (*fatal)(int), int code) {
    fatal(code);
    return -1;
}

double probe_padded_scale(struct probe_padded padded) {
    return (double)padded.left * padded.scale + (double)padded.hidden;
}

static const void *probe_userdata_tokens[4];
static int probe_userdata_held;
static probe_retain_fn probe_userdata_retain;
static probe_release_fn probe_userdata_release;

void probe_userdata_store(const void *token, probe_retain_fn retain, probe_release_fn release) {
    probe_userdata_clear();
    probe_userdata_tokens[0] = token;
    probe_userdata_held = 1;
    probe_userdata_retain = retain;
    probe_userdata_release = release;
}

int probe_userdata_share(void) {
    if ((probe_userdata_held == 0) || (probe_userdata_held >= 4) ||
        (probe_userdata_retain == NULL)) {
        return 0;
    }
    /* One retain creates one more obligation; the pointer bits stay equal. */
    probe_userdata_tokens[probe_userdata_held] = probe_userdata_retain(probe_userdata_tokens[0]);
    probe_userdata_held += 1;
    return probe_userdata_held;
}

int probe_userdata_count(void) {
    return probe_userdata_held;
}

void probe_userdata_clear(void) {
    while (probe_userdata_held > 0) {
        probe_userdata_held -= 1;
        if (probe_userdata_release != NULL) {
            probe_userdata_release(probe_userdata_tokens[probe_userdata_held]);
        }
        probe_userdata_tokens[probe_userdata_held] = NULL;
    }
}

int probe_rounding_is_default(void) {
    return fegetround() == FE_TONEAREST;
}

int probe_flags_raised(void) {
    return fetestexcept(FE_ALL_EXCEPT) != 0;
}

void probe_set_rounding_upward(void) {
    (void)fesetround(FE_UPWARD);
}

void probe_raise_division_by_zero(void) {
    (void)feraiseexcept(FE_DIVBYZERO);
}

int probe_call_with_upward(int (*callback)(int), int value) {
    int result;

    (void)fesetround(FE_UPWARD);
    result = callback(value);
    /* The C-origin entry restores the caller's environment on return. */
    if (fegetround() != FE_UPWARD) {
        result = -1;
    }
    (void)fesetround(FE_TONEAREST);
    return result;
}

struct probe_thread_call {
    int (*callback)(int);
    int value;
    int result;
};

static void *probe_thread_main(void *argument) {
    struct probe_thread_call *call = argument;

    call->result = call->callback(call->value);
    return NULL;
}

int probe_call_on_thread(int (*callback)(int), int value) {
    struct probe_thread_call call;
    pthread_t thread;

    call.callback = callback;
    call.value = value;
    call.result = -1;
    if (pthread_create(&thread, NULL, probe_thread_main, &call) != 0) {
        return -2;
    }
    if (pthread_join(thread, NULL) != 0) {
        return -3;
    }
    return call.result;
}

int probe_call_null(int (*callback)(int *)) {
    return callback(NULL);
}

int probe_call_mode(int (*callback)(enum probe_mode), int raw_value) {
    return callback((enum probe_mode)raw_value);
}

wint_t probe_wint_echo(wint_t value) {
    return value;
}

int probe_sum(int count, ...) {
    va_list arguments;
    int total = 0;
    int index;

    va_start(arguments, count);
    for (index = 0; index < count; ++index) {
        total += va_arg(arguments, int);
    }
    va_end(arguments);
    return total;
}
