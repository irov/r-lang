#ifndef BINDINGS_CASES_H
#define BINDINGS_CASES_H

#include <stddef.h>
#include <stdint.h>

/* The declarations r-front --emit=bindings turns into R (B9.2) and those it leaves out. */

#define BIND_LIMIT 0x7fffU
#define BIND_NEGATIVE (-3)
#define BIND_WIDE 5000000000UL
#define BIND_SUM (BIND_LIMIT + 1)
#define BIND_NAME "bindings"
#define BIND_SQUARE(x) ((x) * (x))

enum {
    BIND_FIRST = 1,
    BIND_SECOND = 2
};

typedef enum bind_level {
    BIND_LOW = 0,
    BIND_HIGH = 9
} bind_level;
enum bind_alias {
    BIND_ONE = 1,
    BIND_UNO = 1
};

typedef struct bind_handle bind_handle;
struct bind_node;
typedef int64_t bind_tick;
typedef const char *bind_text;
typedef int (*bind_visit)(const struct bind_node *node, void *context);

struct bind_node {
    bind_tick tick;
    uint8_t tag[4];
    bind_level level;
    struct bind_node *next;
    int (*apply)(struct bind_node *self, bind_text name);
};

typedef struct {
    double x;
    double y;
} bind_point;

struct bind_flags {
    unsigned ready : 1;
    unsigned count : 7;
};

union bind_value {
    int integer;
    double real;
};

struct bind_keyword {
    int task;
};

extern int bind_counter;
extern const size_t bind_size;
extern _Thread_local int bind_slot;

bind_handle *bind_open(bind_text path, int mode);
void bind_close(bind_handle *handle);
int bind_walk(const struct bind_node *head, bind_visit visit, void *context);
bind_point bind_middle(bind_point left, bind_point right);
int bind_names(const char *const *names, size_t count, char **out);
bind_level bind_raise(bind_level level);
int bind_format(bind_handle *handle, const char *format, ...);
int bind_flags_count(struct bind_flags flags);
double bind_value_real(union bind_value *value);
int move(int value);
int bind_renamed(int value) __asm("_bind_original");
static inline int bind_inline(int value) {
    return value + 1;
}
int __bind_reserved(const char *format, ...);

#endif
