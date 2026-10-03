#ifndef R_STD_JSON_H
#define R_STD_JSON_H

#include "r_runtime_dict.h"
#include "r_runtime_list.h"
#include "r_std_string.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct RStdJsonByteView {
    const uint8_t *data;
    size_t length;
} RStdJsonByteView;

typedef enum RStdJsonErrorCode {
    R_STD_JSON_ERROR_SYNTAX = 0,
    R_STD_JSON_ERROR_INVALID_UTF8,
    R_STD_JSON_ERROR_DUPLICATE_KEY,
    R_STD_JSON_ERROR_DEPTH_LIMIT,
    R_STD_JSON_ERROR_SIZE_LIMIT,
    R_STD_JSON_ERROR_UNEXPECTED_EOF,
    R_STD_JSON_ERROR_TYPE,
    R_STD_JSON_ERROR_RANGE,
    R_STD_JSON_ERROR_MISSING_FIELD,
    R_STD_JSON_ERROR_UNKNOWN_FIELD,
    R_STD_JSON_ERROR_AMBIGUOUS_FIELD,
    R_STD_JSON_ERROR_INVALID_STATE,
    R_STD_JSON_ERROR_KEY_CONFLICT
} RStdJsonErrorCode;

typedef struct RStdJsonError {
    RStdJsonErrorCode code;
    size_t offset;
    RStdString pointer;
} RStdJsonError;

typedef enum RStdJsonCallStatus {
    R_STD_JSON_CALL_SUCCESS = 0,
    R_STD_JSON_CALL_JSON_ERROR,
    R_STD_JSON_CALL_ALLOCATION_ERROR,
    R_STD_JSON_CALL_CONTRACT_VIOLATION
} RStdJsonCallStatus;

typedef struct RStdJsonResult {
    RStdJsonCallStatus status;
    RStdJsonError error;
    RStdAllocError allocation_error;
} RStdJsonResult;

typedef enum RStdJsonMode {
    R_STD_JSON_MODE_DOCUMENT = 0,
    R_STD_JSON_MODE_SEQUENCE,
    R_STD_JSON_MODE_ARRAY_ELEMENTS
} RStdJsonMode;

typedef struct RStdJsonOptions {
    size_t max_depth;
    size_t max_value_bytes;
    size_t indent;
    bool reject_unknown_fields;
    bool ignore_case;
    RStdJsonMode mode;
} RStdJsonOptions;

typedef struct RStdJsonEncodeContext {
    RRuntimeAllocator *allocator;
    RStdJsonOptions options;
    size_t depth;
} RStdJsonEncodeContext;

#define R_STD_JSON_DEFAULT_OPTIONS                                                                 \
    ((RStdJsonOptions){256U, 64U * 1024U * 1024U, 0U, false, false, R_STD_JSON_MODE_DOCUMENT})

typedef enum RStdJsonKind {
    R_STD_JSON_NULL = 0,
    R_STD_JSON_BOOLEAN,
    R_STD_JSON_NUMBER,
    R_STD_JSON_STRING,
    R_STD_JSON_ARRAY,
    R_STD_JSON_OBJECT
} RStdJsonKind;

struct RJsonNode;
typedef struct RStdJsonValue {
    struct RJsonNode *node;
} RStdJsonValue;

typedef struct RStdJsonNumber {
    RStdString text;
} RStdJsonNumber;

typedef struct RStdJsonValueResult {
    RStdJsonResult outcome;
    RStdJsonValue value;
} RStdJsonValueResult;

typedef struct RStdJsonStringResult {
    RStdJsonResult outcome;
    RStdString value;
} RStdJsonStringResult;

typedef struct RStdJsonNumberResult {
    RStdJsonResult outcome;
    RStdJsonNumber value;
} RStdJsonNumberResult;

/* Scanner events are compiler/runtime ABI. Text borrows expire on the next feed. No input
 * fragment is retained. A scanner may transfer text ownership with scanner_take_text. */
typedef enum RStdJsonTokenKind {
    R_STD_JSON_TOKEN_OBJECT_BEGIN = 0,
    R_STD_JSON_TOKEN_OBJECT_END,
    R_STD_JSON_TOKEN_ARRAY_BEGIN,
    R_STD_JSON_TOKEN_ARRAY_END,
    R_STD_JSON_TOKEN_KEY,
    R_STD_JSON_TOKEN_STRING,
    R_STD_JSON_TOKEN_NUMBER,
    R_STD_JSON_TOKEN_TRUE,
    R_STD_JSON_TOKEN_FALSE,
    R_STD_JSON_TOKEN_NULL
} RStdJsonTokenKind;

typedef enum RStdJsonFeedState {
    R_STD_JSON_FEED_TOKEN = 0,
    R_STD_JSON_FEED_NEED_INPUT,
    R_STD_JSON_FEED_VALUE_READY,
    R_STD_JSON_FEED_END
} RStdJsonFeedState;

typedef struct RStdJsonToken {
    RStdJsonTokenKind kind;
    size_t offset;
    RStdJsonByteView text;
} RStdJsonToken;

struct RJsonScannerState;
struct RJsonDecoderState;
typedef struct RStdJsonScanner {
    struct RJsonScannerState *state;
} RStdJsonScanner;

typedef struct RStdJsonFeedResult {
    RStdJsonResult outcome;
    RStdJsonFeedState state;
    size_t consumed;
    RStdJsonToken token;
} RStdJsonFeedResult;

/* Concrete converter ABI. A cursor is a call-bounded scanner over one borrowed document. */
typedef struct RStdJsonCursor {
    RStdJsonScanner scanner;
    RRuntimeAllocator *allocator;
    RStdJsonOptions options;
    RStdJsonByteView input;
    size_t consumed;
    RStdJsonToken token;
    RStdJsonResult outcome;
    bool has_token;
    bool complete;
    struct RJsonDecoderState *decoder;
} RStdJsonCursor;

/* A converter frame contains concrete generated state. No input fragment or field schema is
 * retained by the runtime. Completed children transfer ownership to their parent before feed
 * returns; incomplete frames destroy only components they have initialized. */
typedef enum RStdJsonDecodeStep {
    R_STD_JSON_DECODE_WAIT = 0,
    R_STD_JSON_DECODE_PROGRESS,
    R_STD_JSON_DECODE_COMPLETE,
    R_STD_JSON_DECODE_ERROR
} RStdJsonDecodeStep;
typedef struct RStdJsonDecodeFrame RStdJsonDecodeFrame;
typedef RStdJsonDecodeStep (*RStdJsonDecodeStepFn)(RStdJsonCursor *, RStdJsonDecodeFrame *);
typedef void (*RStdJsonDecodeDropFn)(RStdJsonDecodeFrame *, bool);
typedef bool (*RStdJsonDecodeCreateFn)(RStdJsonCursor *, void *, bool);
struct RStdJsonDecodeFrame {
    RStdJsonDecodeFrame *parent;
    RStdJsonDecodeStepFn step;
    RStdJsonDecodeDropFn drop;
    void *output;
    size_t alignment;
    uint32_t state;
    bool quoted;
};
typedef struct RStdJsonDecoder {
    struct RJsonDecoderState *state;
} RStdJsonDecoder;
typedef enum RStdJsonDecoderStatus {
    R_STD_JSON_DECODER_NEED_INPUT = 0,
    R_STD_JSON_DECODER_VALUE_READY,
    R_STD_JSON_DECODER_END
} RStdJsonDecoderStatus;
typedef struct RStdJsonProgress {
    size_t consumed;
    RStdJsonDecoderStatus state;
} RStdJsonProgress;
/* frames holds the open containers, children their completed members and elements and key_bytes
 * the keys of open objects; a container node is created when it closes and takes its children in
 * one allocation. */
typedef struct RStdJsonTreeBuilder {
    RRuntimeArray frames;
    RRuntimeArray children;
    RRuntimeArray key_bytes;
    RStdJsonValue value;
    bool started;
    bool complete;
} RStdJsonTreeBuilder;

void r_json_tree_builder_initialize(RStdJsonTreeBuilder *builder, RRuntimeAllocator *allocator);
bool r_json_tree_builder_token(RStdJsonTreeBuilder *builder, RStdJsonCursor *cursor, bool quoted);
void r_json_tree_builder_destroy(RStdJsonTreeBuilder *builder);
RStdJsonResult r_json_decoder_initialize(RStdJsonDecoder *decoder,
                                         RRuntimeAllocator *allocator,
                                         RStdJsonOptions options,
                                         RRuntimeTypeInfo result_type,
                                         RStdJsonDecodeCreateFn create);
RStdJsonFeedResult
r_json_decoder_feed(RStdJsonDecoder *decoder, RStdJsonByteView input, bool final);
RStdJsonResult r_json_decoder_take(RStdJsonDecoder *decoder, void *output);
/* Between values only: preserve scanner position while selecting the next concrete codec. */
RStdJsonResult r_json_decoder_rebind(RStdJsonDecoder *decoder,
                                     RRuntimeTypeInfo result_type,
                                     RStdJsonDecodeCreateFn create);
void r_json_decoder_abort(RStdJsonDecoder *decoder);
void r_json_decoder_destroy(RStdJsonDecoder *decoder);
static inline void r_std_json_decoder_destroy(RStdJsonDecoder *decoder) {
    r_json_decoder_destroy(decoder);
}
static inline void r_std_json_decoder_move_initialize(RStdJsonDecoder *destination,
                                                      RStdJsonDecoder *source) {
    *destination = *source;
    *source = (RStdJsonDecoder){0};
}
bool r_json_decode_value_create(RStdJsonCursor *cursor, void *output, bool quoted);
bool r_json_decode_skip_create(RStdJsonCursor *cursor, void *output, bool quoted);
bool r_json_decode_push(RStdJsonCursor *cursor,
                        size_t size,
                        size_t alignment,
                        RStdJsonDecodeStepFn step,
                        RStdJsonDecodeDropFn drop,
                        void *output,
                        bool quoted);

RStdJsonResult r_json_cursor_initialize(RStdJsonCursor *cursor,
                                        RRuntimeAllocator *allocator,
                                        RStdJsonByteView input,
                                        RStdJsonOptions options);
bool r_json_cursor_next(RStdJsonCursor *cursor);
bool r_json_cursor_expect(RStdJsonCursor *cursor, RStdJsonTokenKind kind);
bool r_json_cursor_skip(RStdJsonCursor *cursor);
bool r_json_cursor_value(RStdJsonCursor *cursor, RStdJsonValue *value, bool quoted);
RStdJsonResult r_json_quote_number(RStdJsonValue *value);
RStdJsonValueResult r_json_clone_value(RRuntimeAllocator *allocator, const RStdJsonValue *value);
RStdJsonResult r_json_object_extend(RStdJsonValue *target, RStdJsonValue *source);
bool r_json_cursor_number(RStdJsonCursor *cursor, RStdJsonNumber *value, bool quoted);
bool r_json_cursor_integer(RStdJsonCursor *cursor,
                           bool quoted,
                           uint64_t positive_limit,
                           uint64_t negative_limit,
                           bool *negative,
                           uint64_t *magnitude);
bool r_json_cursor_float(RStdJsonCursor *cursor, bool quoted, bool binary32, double *value);
bool r_json_cursor_long_double(RStdJsonCursor *cursor, bool quoted, long double *value);
bool r_json_cursor_string(RStdJsonCursor *cursor, RStdString *value);
bool r_json_cursor_boolean(RStdJsonCursor *cursor, bool *value);
bool r_json_cursor_char(RStdJsonCursor *cursor, uint32_t *value);
bool r_json_cursor_default_string(RStdJsonCursor *cursor,
                                  RStdString *value,
                                  RStdJsonByteView source);
bool r_json_cursor_array_push(RStdJsonCursor *cursor, RRuntimeArray *array, void *value);
bool r_json_cursor_list_push(RStdJsonCursor *cursor, RRuntimeList *list, void *value);
void r_json_cursor_dict_initialize(RStdJsonCursor *cursor,
                                   RRuntimeDict *dict,
                                   RRuntimeTypeInfo value);
bool r_json_cursor_dict_insert(RStdJsonCursor *cursor,
                               RRuntimeDict *dict,
                               RStdString *key,
                               void *value);
bool r_json_cursor_fail(RStdJsonCursor *cursor, RStdJsonErrorCode code);
RStdJsonResult r_json_cursor_finish(RStdJsonCursor *cursor);
void r_json_cursor_destroy(RStdJsonCursor *cursor);

/* Operations return owned error pointers on JSON_ERROR. On allocation failure no partially
 * constructed value escapes. All mutating tree operations commit only after allocation succeeds. */
RStdJsonValueResult r_std_json_parse(RRuntimeAllocator *allocator, RStdJsonByteView source);
RStdJsonValueResult r_std_json_parse_with_options(RRuntimeAllocator *allocator,
                                                  RStdJsonByteView source,
                                                  RStdJsonOptions options);
RStdJsonStringResult r_std_json_stringify(RRuntimeAllocator *allocator, const RStdJsonValue *value);
RStdJsonStringResult r_std_json_stringify_with_options(RRuntimeAllocator *allocator,
                                                       const RStdJsonValue *value,
                                                       RStdJsonOptions options);
RStdJsonKind r_std_json_kind(const RStdJsonValue *value);
size_t r_std_json_len(const RStdJsonValue *value);
const RStdJsonValue *r_std_json_get(const RStdJsonValue *value, size_t index);
const RStdJsonValue *r_std_json_find(const RStdJsonValue *value, RStdJsonByteView key);
RStdJsonByteView r_std_json_key_at(const RStdJsonValue *value, size_t index);
RStdJsonByteView r_std_json_text(const RStdJsonValue *value);
bool r_std_json_boolean(const RStdJsonValue *value);
RStdJsonValue r_std_json_null(void);
RStdJsonValueResult r_std_json_from_bool(RRuntimeAllocator *allocator, bool value);
RStdJsonValueResult r_std_json_from_string(RRuntimeAllocator *allocator, RStdJsonByteView value);
RStdJsonValueResult r_std_json_from_number(RRuntimeAllocator *allocator,
                                           const RStdJsonNumber *value);
RStdJsonValueResult r_std_json_array(RRuntimeAllocator *allocator);
RStdJsonValueResult r_std_json_object(RRuntimeAllocator *allocator);
RStdJsonResult r_std_json_append(RStdJsonValue *target, RStdJsonValue *value);
RStdJsonResult r_std_json_insert(RStdJsonValue *target, RStdJsonByteView key, RStdJsonValue *value);
RStdJsonValueResult r_std_json_take_index(RStdJsonValue *target, size_t index);
RStdJsonValueResult r_std_json_take_field(RStdJsonValue *target, RStdJsonByteView key);
RStdJsonNumberResult r_std_json_parse_number(RRuntimeAllocator *allocator, RStdJsonByteView source);
RStdJsonByteView r_std_json_number_text(const RStdJsonNumber *value);
bool r_std_json_number_is_zero(const RStdJsonNumber *value);
/* Unicode 17.0.0 simple folding; '_' and '-' are ignored; no locale or normalization. */
bool r_std_json_name_equal(RStdJsonByteView left, RStdJsonByteView right, bool ignore_case);

RStdJsonValueResult
r_json_encode_integer(RRuntimeAllocator *allocator, bool negative, uint64_t magnitude, bool quoted);
RStdJsonValueResult r_json_encode_float(RRuntimeAllocator *allocator,
                                        long double value,
                                        uint32_t representation,
                                        bool quoted);
RStdJsonValueResult r_json_encode_char(RRuntimeAllocator *allocator, uint32_t scalar);
bool r_json_value_empty(const RStdJsonValue *value);

/* Compiler ownership glue and scanner ABI, not additional source-language operations. */
void r_json_value_destroy(RStdJsonValue *value);
void r_json_error_destroy(RStdJsonError *error);
void r_json_number_destroy(RStdJsonNumber *number);
RStdJsonResult r_json_scanner_initialize(RStdJsonScanner *scanner,
                                         RRuntimeAllocator *allocator,
                                         RStdJsonOptions options);
RStdJsonFeedResult
r_json_scanner_feed(RStdJsonScanner *scanner, RStdJsonByteView input, bool final);
RStdString r_json_scanner_take_text(RStdJsonScanner *scanner);
void r_json_scanner_destroy(RStdJsonScanner *scanner);
RStdJsonResult
r_json_scanner_failure(RStdJsonScanner *scanner, RStdJsonErrorCode code, size_t offset);

static inline void r_std_json_value_move_initialize(RStdJsonValue *destination,
                                                    RStdJsonValue *source) {
    *destination = *source;
    *source = (RStdJsonValue){0};
}
static inline void r_std_json_value_destroy(RStdJsonValue *source) {
    r_json_value_destroy(source);
}

static inline void r_std_json_number_move_initialize(RStdJsonNumber *destination,
                                                     RStdJsonNumber *source) {
    *destination = *source;
    *source = (RStdJsonNumber){0};
}
static inline void r_std_json_number_destroy(RStdJsonNumber *source) {
    r_json_number_destroy(source);
}

static inline void r_std_json_error_move_initialize(RStdJsonError *destination,
                                                    RStdJsonError *source) {
    *destination = *source;
    *source = (RStdJsonError){0};
}
static inline void r_std_json_error_destroy(RStdJsonError *source) {
    r_json_error_destroy(source);
}

#endif
