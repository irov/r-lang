#include "r_std_json.h"

#include <stdint.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

typedef struct RJsonFuzzTrace {
    uint64_t hash;
    size_t offset;
    RStdJsonCallStatus status;
    RStdJsonErrorCode code;
} RJsonFuzzTrace;

static RJsonFuzzTrace r_json_fuzz_scan(const uint8_t *data, size_t size, size_t chunk) {
    RRuntimeAllocator allocator;
    RStdJsonScanner scanner;
    RStdJsonOptions options = R_STD_JSON_DEFAULT_OPTIONS;
    RStdJsonResult created;
    RJsonFuzzTrace trace = {UINT64_C(14695981039346656037), 0U, R_STD_JSON_CALL_SUCCESS, 0};
    size_t offset = 0U;
    options.max_value_bytes = 65536U;
    r_runtime_allocator_initialize(&allocator);
    created = r_json_scanner_initialize(&scanner, &allocator, options);
    if (created.status != R_STD_JSON_CALL_SUCCESS)
        abort();
    for (;;) {
        size_t length = size - offset;
        size_t used = 0U;
        bool stop = false;
        if (length > chunk)
            length = chunk;
        for (;;) {
            RStdJsonFeedResult fed =
                r_json_scanner_feed(&scanner,
                                    (RStdJsonByteView){data + offset + used, length - used},
                                    offset + length == size);
            used += fed.consumed;
            trace.status = fed.outcome.status;
            trace.code = fed.outcome.error.code;
            trace.offset = fed.outcome.error.offset;
            if (fed.outcome.status != R_STD_JSON_CALL_SUCCESS) {
                r_json_error_destroy(&fed.outcome.error);
                stop = true;
                break;
            }
            if (fed.state == R_STD_JSON_FEED_END) {
                stop = true;
                break;
            }
            if (fed.state == R_STD_JSON_FEED_NEED_INPUT)
                break;
            trace.hash ^= (uint64_t)fed.state;
            trace.hash *= UINT64_C(1099511628211);
            if (fed.state == R_STD_JSON_FEED_TOKEN) {
                trace.hash ^= (uint64_t)fed.token.kind;
                trace.hash *= UINT64_C(1099511628211);
                for (size_t i = 0U; i < fed.token.text.length; ++i) {
                    trace.hash ^= fed.token.text.data[i];
                    trace.hash *= UINT64_C(1099511628211);
                }
            }
        }
        offset += used;
        if (stop)
            break;
        if (used != length || offset == size)
            abort();
    }
    r_json_scanner_destroy(&scanner);
    return trace;
}
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    RJsonFuzzTrace whole;
    RJsonFuzzTrace split;
    RRuntimeAllocator allocator;
    RStdJsonValueResult value;
    if (size > 65536U)
        return 0;
    whole = r_json_fuzz_scan(data, size, size + 1U);
    split = r_json_fuzz_scan(data, size, size == 0U ? 1U : (size_t)(data[0] % 31U) + 1U);
    if (whole.status != split.status || whole.code != split.code || whole.offset != split.offset ||
        whole.hash != split.hash)
        abort();
    r_runtime_allocator_initialize(&allocator);
    value = r_std_json_parse(&allocator, (RStdJsonByteView){data, size});
    if (value.outcome.status == R_STD_JSON_CALL_SUCCESS) {
        RStdJsonStringResult text = r_std_json_stringify(&allocator, &value.value);
        if (text.outcome.status == R_STD_JSON_CALL_SUCCESS) {
            RStdJsonValueResult again =
                r_std_json_parse(&allocator,
                                 (RStdJsonByteView){r_runtime_string_bytes(&text.value),
                                                    r_runtime_string_length(&text.value)});
            if (again.outcome.status != R_STD_JSON_CALL_SUCCESS)
                abort();
            r_json_value_destroy(&again.value);
        }
        r_runtime_string_destroy(&text.value);
        r_json_error_destroy(&text.outcome.error);
    }
    r_json_value_destroy(&value.value);
    r_json_error_destroy(&value.outcome.error);
    return 0;
}
