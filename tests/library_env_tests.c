#include "r_std_env.h"

#include "r_runtime_0_1.h"
#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_dict.h"
#include "r_runtime_string.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr,                                                                  \
                          "environment test failure at %s:%d: %s\n",                               \
                          __FILE__,                                                                \
                          __LINE__,                                                                \
                          #expression);                                                            \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestEnvThreadState {
    char name[96];
    char value[32];
    _Bool passed;
} RTestEnvThreadState;

static RStdStringView r_test_env_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static _Bool r_test_env_string_equals(const RRuntimeString *string, const char *expected) {
    const size_t length = strlen(expected);

    return (r_runtime_string_length(string) == length) &&
           ((length == 0U) || (memcmp(r_runtime_string_bytes(string), expected, length) == 0));
}

static _Bool
r_test_env_error_is(RStdEnvCallStatus status, RStdEnvError error, RStdEnvErrorCode code) {
    return (status == R_STD_ENV_CALL_ERROR) && (error.code == code) &&
           (error.native_code == INT64_C(0));
}

static const RRuntimeString *r_test_env_dict_value(const RStdDict *dict, const char *name) {
    RRuntimeDictIterator iterator = r_runtime_dict_iter(dict);
    RRuntimeDictEntryRef entry;

    while (r_runtime_dict_next(&iterator, &entry)) {
        const RRuntimeString *key = entry.key;

        if (r_test_env_string_equals(key, name)) {
            return entry.value;
        }
    }
    return NULL;
}

static _Bool r_test_env_arguments(void) {
    static char program[] = "env-test";
    static char unicode_argument[] = "caf\xc3\xa9";
    static char empty_argument[] = "";
    char *arguments[] = {program, unicode_argument, empty_argument};
    RRuntimeAllocator allocator;
    RStdEnvArgumentsResult result;
    RRuntimeStartResult start;
    const RRuntimeString *first;
    const RRuntimeString *second;
    const RRuntimeString *third;
    uint64_t allocation_attempts;
    uint64_t fail_at;

    start = r_runtime_hosted_start(3, arguments);
    R_TEST_CHECK(start.started && (start.process_status == 0));

    r_runtime_allocator_initialize(&allocator);
    result = r_std_env_arguments(&allocator);
    R_TEST_CHECK(result.status == R_STD_ENV_CALL_SUCCESS);
    R_TEST_CHECK(result.value.length == 3U);
    R_TEST_CHECK(r_runtime_hosted_finish(INT32_C(0)) == 0);

    first = r_runtime_array_get(&result.value, 0U);
    second = r_runtime_array_get(&result.value, 1U);
    third = r_runtime_array_get(&result.value, 2U);
    R_TEST_CHECK((first != NULL) && r_test_env_string_equals(first, program));
    R_TEST_CHECK((second != NULL) && r_test_env_string_equals(second, unicode_argument));
    R_TEST_CHECK((third != NULL) && r_test_env_string_equals(third, empty_argument));
    r_runtime_array_destroy(&result.value);

    start = r_runtime_hosted_start(3, arguments);
    R_TEST_CHECK(start.started && (start.process_status == 0));
    r_runtime_allocator_initialize(&allocator);
    result = r_std_env_arguments(&allocator);
    R_TEST_CHECK(result.status == R_STD_ENV_CALL_SUCCESS);
    allocation_attempts = r_runtime_allocator_attempt_count(&allocator);
    R_TEST_CHECK(allocation_attempts > UINT64_C(0));
    r_runtime_array_destroy(&result.value);
    for (fail_at = UINT64_C(1); fail_at <= allocation_attempts; ++fail_at) {
        r_runtime_allocator_initialize(&allocator);
        r_runtime_allocator_set_failure(&allocator, fail_at);
        result = r_std_env_arguments(&allocator);
        R_TEST_CHECK(
            r_test_env_error_is(result.status, result.error, R_STD_ENV_ERROR_ALLOCATION_FAILED));
        R_TEST_CHECK((result.value.data == NULL) && (result.value.length == 0U) &&
                     (result.value.capacity == 0U));
    }
    R_TEST_CHECK(r_runtime_hosted_finish(INT32_C(0)) == 0);
    return 1;
}

static _Bool r_test_env_validation_precedence(const char *name) {
    static const uint8_t invalid_name_bytes[] = {'b', 'a', 'd', '=', 'n', 'a', 'm', 'e'};
    static const uint8_t invalid_value_bytes[] = {'b', 'a', 'd', UINT8_C(0), 'v'};
    RRuntimeAllocator allocator;
    RStdEnvVoidResult result;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    result = r_std_env_set(&allocator,
                           (RStdStringView){invalid_name_bytes, sizeof(invalid_name_bytes)},
                           (RStdStringView){invalid_value_bytes, sizeof(invalid_value_bytes)});
    R_TEST_CHECK(r_test_env_error_is(result.status, result.error, R_STD_ENV_ERROR_INVALID_NAME));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    result = r_std_env_set(&allocator,
                           r_test_env_view(name),
                           (RStdStringView){invalid_value_bytes, sizeof(invalid_value_bytes)});
    R_TEST_CHECK(r_test_env_error_is(result.status, result.error, R_STD_ENV_ERROR_INVALID_VALUE));
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    r_runtime_allocator_initialize(&allocator);
    result = r_std_env_remove(&allocator, (RStdStringView){NULL, 0U});
    R_TEST_CHECK(r_test_env_error_is(result.status, result.error, R_STD_ENV_ERROR_INVALID_NAME));
    return 1;
}

static _Bool r_test_env_get_set_remove(const char *name) {
    RRuntimeAllocator allocator;
    RStdEnvVoidResult mutation;
    RStdEnvGetResult first;
    RStdEnvGetResult second;

    r_runtime_allocator_initialize(&allocator);
    mutation = r_std_env_remove(&allocator, r_test_env_view(name));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);
    first = r_std_env_get(&allocator, r_test_env_view(name));
    R_TEST_CHECK((first.status == R_STD_ENV_CALL_SUCCESS) && !first.has_value);

    mutation = r_std_env_set(&allocator, r_test_env_view(name), r_test_env_view("alpha"));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);
    first = r_std_env_get(&allocator, r_test_env_view(name));
    R_TEST_CHECK((first.status == R_STD_ENV_CALL_SUCCESS) && first.has_value &&
                 r_test_env_string_equals(&first.value, "alpha"));

    mutation = r_std_env_set(&allocator, r_test_env_view(name), r_test_env_view("beta"));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);
    second = r_std_env_get(&allocator, r_test_env_view(name));
    R_TEST_CHECK((second.status == R_STD_ENV_CALL_SUCCESS) && second.has_value &&
                 r_test_env_string_equals(&second.value, "beta"));
    R_TEST_CHECK(r_test_env_string_equals(&first.value, "alpha"));
    r_runtime_string_destroy(&second.value);
    r_runtime_string_destroy(&first.value);

    mutation = r_std_env_remove(&allocator, r_test_env_view(name));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);
    mutation = r_std_env_remove(&allocator, r_test_env_view(name));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);
    first = r_std_env_get(&allocator, r_test_env_view(name));
    R_TEST_CHECK((first.status == R_STD_ENV_CALL_SUCCESS) && !first.has_value);
    return 1;
}

static _Bool r_test_env_mutation_allocation_failure(const char *name) {
    RRuntimeAllocator allocator;
    RStdEnvVoidResult mutation;
    RStdEnvGetResult query;
    const char *native_value;

    r_runtime_allocator_initialize(&allocator);
    mutation = r_std_env_set(&allocator, r_test_env_view(name), r_test_env_view("stable"));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    mutation = r_std_env_set(&allocator, r_test_env_view(name), r_test_env_view("changed"));
    R_TEST_CHECK(
        r_test_env_error_is(mutation.status, mutation.error, R_STD_ENV_ERROR_ALLOCATION_FAILED));
    native_value = getenv(name);
    R_TEST_CHECK((native_value != NULL) && (strcmp(native_value, "stable") == 0));

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(2));
    mutation = r_std_env_set(&allocator, r_test_env_view(name), r_test_env_view("changed"));
    R_TEST_CHECK(
        r_test_env_error_is(mutation.status, mutation.error, R_STD_ENV_ERROR_ALLOCATION_FAILED));
    native_value = getenv(name);
    R_TEST_CHECK((native_value != NULL) && (strcmp(native_value, "stable") == 0));

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    query = r_std_env_get(&allocator, r_test_env_view(name));
    R_TEST_CHECK(r_test_env_error_is(query.status, query.error, R_STD_ENV_ERROR_ALLOCATION_FAILED));
    R_TEST_CHECK(!query.has_value);

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(2));
    query = r_std_env_get(&allocator, r_test_env_view(name));
    R_TEST_CHECK(r_test_env_error_is(query.status, query.error, R_STD_ENV_ERROR_ALLOCATION_FAILED));
    R_TEST_CHECK(!query.has_value);

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    mutation = r_std_env_remove(&allocator, r_test_env_view(name));
    R_TEST_CHECK(
        r_test_env_error_is(mutation.status, mutation.error, R_STD_ENV_ERROR_ALLOCATION_FAILED));
    native_value = getenv(name);
    R_TEST_CHECK((native_value != NULL) && (strcmp(native_value, "stable") == 0));

    r_runtime_allocator_initialize(&allocator);
    mutation = r_std_env_remove(&allocator, r_test_env_view(name));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);
    return 1;
}

static _Bool r_test_env_variables(const char *name) {
    RRuntimeAllocator allocator;
    RStdEnvVoidResult mutation;
    RStdEnvVariablesResult snapshot;
    const RRuntimeString *value;
    uint64_t allocation_attempts;
    uint64_t fail_at;

    r_runtime_allocator_initialize(&allocator);
    mutation = r_std_env_set(&allocator, r_test_env_view(name), r_test_env_view("snapshot"));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);

    r_runtime_allocator_initialize(&allocator);
    snapshot = r_std_env_variables(&allocator, UINT64_C(0x123456789abcdef0));
    R_TEST_CHECK(snapshot.status == R_STD_ENV_CALL_SUCCESS);
    value = r_test_env_dict_value(&snapshot.value, name);
    R_TEST_CHECK((value != NULL) && r_test_env_string_equals(value, "snapshot"));

    mutation = r_std_env_set(&allocator, r_test_env_view(name), r_test_env_view("later"));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);
    value = r_test_env_dict_value(&snapshot.value, name);
    R_TEST_CHECK((value != NULL) && r_test_env_string_equals(value, "snapshot"));
    r_runtime_dict_destroy(&snapshot.value);

    r_runtime_allocator_initialize(&allocator);
    snapshot = r_std_env_variables(&allocator, UINT64_C(7));
    R_TEST_CHECK(snapshot.status == R_STD_ENV_CALL_SUCCESS);
    allocation_attempts = r_runtime_allocator_attempt_count(&allocator);
    R_TEST_CHECK(allocation_attempts > UINT64_C(0));
    r_runtime_dict_destroy(&snapshot.value);
    for (fail_at = UINT64_C(1); fail_at <= allocation_attempts; ++fail_at) {
        r_runtime_allocator_initialize(&allocator);
        r_runtime_allocator_set_failure(&allocator, fail_at);
        snapshot = r_std_env_variables(&allocator, UINT64_C(7));
        R_TEST_CHECK(r_test_env_error_is(
            snapshot.status, snapshot.error, R_STD_ENV_ERROR_ALLOCATION_FAILED));
        R_TEST_CHECK((snapshot.value.slots == NULL) && (snapshot.value.length == 0U));
    }

    r_runtime_allocator_initialize(&allocator);
    mutation = r_std_env_remove(&allocator, r_test_env_view(name));
    R_TEST_CHECK(mutation.status == R_STD_ENV_CALL_SUCCESS);
    return 1;
}

static _Bool r_test_env_invalid_native_snapshot(const char *name) {
    static const char invalid_value[] = {(char)0xff, '\0'};
    RRuntimeAllocator allocator;
    RStdEnvVariablesResult snapshot;

    R_TEST_CHECK(setenv(name, invalid_value, 1) == 0);
    r_runtime_allocator_initialize(&allocator);
    snapshot = r_std_env_variables(&allocator, UINT64_C(11));
    R_TEST_CHECK(
        r_test_env_error_is(snapshot.status, snapshot.error, R_STD_ENV_ERROR_INVALID_VALUE));
    R_TEST_CHECK((snapshot.value.slots == NULL) && (snapshot.value.length == 0U));
    R_TEST_CHECK(unsetenv(name) == 0);
    return 1;
}

static void *r_test_env_thread_main(void *context) {
    RTestEnvThreadState *state = context;
    RRuntimeAllocator allocator;
    size_t iteration;

    state->passed = 0;
    r_runtime_allocator_initialize(&allocator);
    for (iteration = 0U; iteration < 100U; ++iteration) {
        RStdEnvVoidResult mutation =
            r_std_env_set(&allocator, r_test_env_view(state->name), r_test_env_view(state->value));
        RStdEnvGetResult query;

        if (mutation.status != R_STD_ENV_CALL_SUCCESS) {
            return NULL;
        }
        query = r_std_env_get(&allocator, r_test_env_view(state->name));
        if ((query.status != R_STD_ENV_CALL_SUCCESS) || !query.has_value ||
            !r_test_env_string_equals(&query.value, state->value)) {
            if (query.has_value) {
                r_runtime_string_destroy(&query.value);
            }
            return NULL;
        }
        r_runtime_string_destroy(&query.value);
    }
    if (r_std_env_remove(&allocator, r_test_env_view(state->name)).status !=
        R_STD_ENV_CALL_SUCCESS) {
        return NULL;
    }
    state->passed = 1;
    return NULL;
}

static _Bool r_test_env_concurrent(void) {
    enum {
        THREAD_COUNT = 4
    };
    pthread_t threads[THREAD_COUNT];
    RTestEnvThreadState states[THREAD_COUNT] = {0};
    size_t created = 0U;
    size_t index;
    _Bool passed = 1;

    for (index = 0U; index < THREAD_COUNT; ++index) {
        if ((snprintf(states[index].name,
                      sizeof(states[index].name),
                      "R_STD_ENV_THREAD_%ld_%zu",
                      (long)getpid(),
                      index) <= 0) ||
            (snprintf(states[index].value, sizeof(states[index].value), "value_%zu", index) <= 0)) {
            passed = 0;
            break;
        }
        (void)unsetenv(states[index].name);
        if (pthread_create(&threads[index], NULL, r_test_env_thread_main, &states[index]) != 0) {
            passed = 0;
            break;
        }
        created += 1U;
    }
    for (index = 0U; index < created; ++index) {
        if ((pthread_join(threads[index], NULL) != 0) || !states[index].passed) {
            passed = 0;
        }
        (void)unsetenv(states[index].name);
    }
    if (!passed) {
        (void)fprintf(stderr, "environment test failure: concurrent operation failed\n");
    }
    return passed;
}

static _Bool r_test_env_as_error(void) {
    const RStdError converted =
        r_std_env_as_error((RStdEnvError){R_STD_ENV_ERROR_OTHER, -INT64_C(17)});

    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_ENVIRONMENT);
    R_TEST_CHECK(converted.code == (uint32_t)R_STD_ENV_ERROR_OTHER);
    R_TEST_CHECK(converted.native_code == -INT64_C(17));
    return 1;
}

int main(void) {
    char name[96];
    char invalid_name[96];

    if ((snprintf(name, sizeof(name), "R_STD_ENV_TEST_%ld", (long)getpid()) <= 0) ||
        (snprintf(
             invalid_name, sizeof(invalid_name), "R_STD_ENV_INVALID_TEST_%ld", (long)getpid()) <=
         0)) {
        (void)fprintf(stderr, "environment test failure: could not construct unique names\n");
        return EXIT_FAILURE;
    }
    (void)unsetenv(name);
    (void)unsetenv(invalid_name);

    if (!r_test_env_arguments() || !r_test_env_validation_precedence(name) ||
        !r_test_env_get_set_remove(name) || !r_test_env_mutation_allocation_failure(name) ||
        !r_test_env_variables(name) || !r_test_env_invalid_native_snapshot(invalid_name) ||
        !r_test_env_concurrent() || !r_test_env_as_error()) {
        (void)unsetenv(name);
        (void)unsetenv(invalid_name);
        return EXIT_FAILURE;
    }

    (void)unsetenv(name);
    (void)unsetenv(invalid_name);
    (void)fprintf(stdout, "library_env_tests: ok\n");
    return EXIT_SUCCESS;
}
