#ifndef R_STD_JSON_READER_H
#define R_STD_JSON_READER_H

#include "r_std_json.h"
#include "r_std_time.h"

struct RJsonReaderState;
typedef struct RStdJsonReader {
    struct RJsonReaderState *state;
} RStdJsonReader;
typedef struct RStdJsonDetached {
    struct RJsonReaderState *state;
} RStdJsonDetached;

typedef struct RStdJsonDeadline {
    bool has_value;
    RStdTimeInstant value;
} RStdJsonDeadline;
typedef struct RStdJsonTaskStartResult {
    bool is_ok;
    RRuntimeTask *task;
    RStdAsyncStartError error;
} RStdJsonTaskStartResult;
typedef enum RStdJsonReadStatus {
    R_STD_JSON_READ_VALUE = 0,
    R_STD_JSON_READ_END,
    R_STD_JSON_READ_JSON_ERROR,
    R_STD_JSON_READ_ALLOCATION_ERROR,
    R_STD_JSON_READ_TRANSPORT_ERROR
} RStdJsonReadStatus;
typedef struct RStdJsonReadOutcome {
    RStdJsonReadStatus status;
    RStdJsonResult json;
    uint32_t transport_code;
    int64_t native_code;
} RStdJsonReadOutcome;

/* Concrete adapters are generated beside the codec. Native result storage has the actual
 * transport type; its consuming callback returns the reusable buffer without aliasing casts. */
typedef struct RStdJsonTransportRead {
    RRuntimeArray buffer;
    size_t count;
    bool end;
    bool failed;
    uint32_t code;
    int64_t native_code;
} RStdJsonTransportRead;
typedef struct RStdJsonTransport {
    RRuntimeTypeInfo handle_type;
    RRuntimeTypeInfo read_type;
    RStdJsonTaskStartResult (*start)(const void *, RRuntimeArray *, RStdJsonDeadline);
    void (*take)(void *, RStdJsonTransportRead *);
    bool (*deadline)(RStdJsonDeadline, RStdJsonReadOutcome *);
} RStdJsonTransport;

/* VALUE consumes the decoder result into the concrete checked carrier. Other outcomes consume
 * their error payload. The callback cannot allocate or fail. */
typedef void (*RStdJsonReadCompleteFn)(void *, RStdJsonDecoder *, RStdJsonReadOutcome);

/* All reservations precede moving handle. Failure leaves it initialized and unchanged. */
RStdJsonResult r_json_reader_initialize(RStdJsonReader *reader,
                                        RRuntimeAllocator *allocator,
                                        RStdJsonOptions options,
                                        RStdJsonTransport transport,
                                        void *handle);
/* Start failure changes neither reader nor its scanner. Committed operations retain native
 * state independently of the source shell. Concurrent operations report invalid_state. */
RStdJsonTaskStartResult r_json_reader_read_next(RStdJsonReader *reader,
                                                RStdJsonDeadline deadline,
                                                RRuntimeTypeInfo value_type,
                                                RStdJsonDecodeCreateFn create,
                                                RRuntimeTypeInfo result_type,
                                                RStdJsonReadCompleteFn complete);
RStdJsonResult r_json_reader_detach(RStdJsonReader *reader, RStdJsonDetached *detached);
RStdJsonResult r_json_detached_take_handle(RStdJsonDetached *detached, void *output);
RStdJsonResult r_json_detached_take_bytes(RStdJsonDetached *detached, RRuntimeArray *output);
void r_json_reader_destroy(RStdJsonReader *reader);
void r_json_detached_destroy(RStdJsonDetached *detached);

static inline void r_std_json_reader_move_initialize(RStdJsonReader *destination,
                                                     RStdJsonReader *source) {
    *destination = *source;
    *source = (RStdJsonReader){0};
}
static inline void r_std_json_reader_destroy(RStdJsonReader *reader) {
    r_json_reader_destroy(reader);
}
static inline void r_std_json_detached_move_initialize(RStdJsonDetached *destination,
                                                       RStdJsonDetached *source) {
    *destination = *source;
    *source = (RStdJsonDetached){0};
}
static inline void r_std_json_detached_destroy(RStdJsonDetached *detached) {
    r_json_detached_destroy(detached);
}

#endif
