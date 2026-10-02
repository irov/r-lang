#include "r_std_c.h"

#include "r_runtime_0_1.h"

#include <fenv.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#pragma STDC FENV_ACCESS ON

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int r_test_destructor_count;

static RStdStringView r_test_view(const uint8_t *data, size_t length) {
    return (RStdStringView){data, length};
}

static RStdStringView r_test_literal_view(const char *data) {
    return (RStdStringView){(const uint8_t *)data, strlen(data)};
}

static void r_test_handle_destructor(void *pointer) {
    int *value = pointer;
    *value += 1;
    r_test_destructor_count += 1;
}

static int r_test_c_strings(void) {
    static const uint8_t source[] = {
        UINT8_C('c'), UINT8_C('a'), UINT8_C('f'), UINT8_C(0xc3), UINT8_C(0xa9)};
    static const uint8_t embedded[] = {UINT8_C('a'), UINT8_C(0), UINT8_C('b')};
    static const char bounded[] = {'o', 'k', '\0', (char)0xff};
    static const char invalid_with_nul[] = {'x', (char)0xe2, '(', (char)0xa1, '\0'};
    static const char invalid_without_nul[] = {'x', (char)0xe2, '('};
    RRuntimeAllocator allocator;
    RStdCStringResult owned;
    RStdCValidateUtf8Result validated;
    RStdCCopyUtf8Result copied;
    RStdCCharSlice slice;

    r_runtime_allocator_initialize(&allocator);
    owned = r_std_c_string_from_str(&allocator, r_test_view(source, sizeof(source)));
    R_TEST_CHECK(owned.status == R_STD_C_CALL_SUCCESS);
    slice = r_std_c_string_as_slice(&owned.value);
    R_TEST_CHECK(slice.length == sizeof(source) + 1U);
    R_TEST_CHECK(memcmp(slice.data, source, sizeof(source)) == 0);
    R_TEST_CHECK(slice.data[sizeof(source)] == '\0');
    R_TEST_CHECK(r_std_c_string_as_ptr(&owned.value) == slice.data);
    r_runtime_array_destroy(&owned.value);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    owned = r_std_c_string_from_str(&allocator, r_test_view(embedded, sizeof(embedded)));
    R_TEST_CHECK(owned.status == R_STD_C_CALL_ERROR);
    R_TEST_CHECK(owned.error.kind == R_STD_C_STRING_ERROR_EMBEDDED_NUL);
    R_TEST_CHECK(owned.error.index == 1U);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));

    validated = r_std_c_validate_utf8((RStdCCharSlice){bounded, sizeof(bounded)});
    R_TEST_CHECK(validated.status == R_STD_C_CALL_SUCCESS);
    R_TEST_CHECK(validated.value.length == 2U);
    R_TEST_CHECK(memcmp(validated.value.data, "ok", 2U) == 0);
    validated =
        r_std_c_validate_utf8((RStdCCharSlice){invalid_without_nul, sizeof(invalid_without_nul)});
    R_TEST_CHECK(validated.status == R_STD_C_CALL_ERROR);
    R_TEST_CHECK(validated.error.kind == R_STD_C_STRING_ERROR_MISSING_NUL);
    validated = r_std_c_validate_utf8((RStdCCharSlice){invalid_with_nul, sizeof(invalid_with_nul)});
    R_TEST_CHECK(validated.status == R_STD_C_CALL_ERROR);
    R_TEST_CHECK(validated.error.kind == R_STD_C_STRING_ERROR_INVALID_UTF8);
    R_TEST_CHECK(validated.error.index == 1U);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    copied = r_std_c_copy_utf8(&allocator,
                               (RStdCCharSlice){invalid_without_nul, sizeof(invalid_without_nul)});
    R_TEST_CHECK(copied.error.kind == R_STD_C_STRING_ERROR_MISSING_NUL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    copied = r_std_c_copy_utf8(&allocator, (RStdCCharSlice){bounded, sizeof(bounded)});
    R_TEST_CHECK(copied.status == R_STD_C_CALL_ERROR);
    R_TEST_CHECK(copied.error.kind == R_STD_C_STRING_ERROR_ALLOCATION_FAILED);
    R_TEST_CHECK(copied.error.allocation_error == R_STD_ALLOC_ERROR_OUT_OF_MEMORY);
    r_runtime_string_destroy(&copied.value);
    return 0;
}

static int r_test_handle(void) {
    int value = 0;
    RStdCHandle handle = r_std_c_adopt_handle(&value, r_test_handle_destructor);
    void *released;

    R_TEST_CHECK(r_std_c_handle_pointer(&handle) == &value);
    r_std_c_handle_destroy(&handle);
    R_TEST_CHECK(value == 1);
    R_TEST_CHECK(r_test_destructor_count == 1);
    r_std_c_handle_destroy(&handle);
    R_TEST_CHECK(r_test_destructor_count == 1);

    handle = r_std_c_adopt_handle(&value, r_test_handle_destructor);
    released = r_std_c_release_handle(&handle);
    R_TEST_CHECK(released == &value);
    r_std_c_handle_destroy(&handle);
    R_TEST_CHECK(r_test_destructor_count == 1);
    r_test_handle_destructor(released);
    R_TEST_CHECK(r_test_destructor_count == 2);
    return 0;
}

typedef struct RTestHandleEnvironment {
    int calls;
    int entry_rounding;
    int entry_flags;
    int change_failed;
} RTestHandleEnvironment;

static void r_test_handle_environment_destructor(void *pointer) {
    RTestHandleEnvironment *state = pointer;

    state->calls += 1;
    state->entry_rounding = fegetround();
    state->entry_flags = fetestexcept(FE_ALL_EXCEPT);
    state->change_failed = fesetround(FE_UPWARD) != 0 || feraiseexcept(FE_INVALID) != 0;
}

static int r_test_handle_environment(void) {
    fenv_t original;
    RTestHandleEnvironment state = {0};
    RStdCHandle handle = r_std_c_adopt_handle(&state, r_test_handle_environment_destructor);
    RStdCHandle moved;

    R_TEST_CHECK(fegetenv(&original) == 0);
    R_TEST_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);
    r_std_c_handle_move_initialize(&moved, &handle);
    r_std_c_handle_destroy(&handle);
    R_TEST_CHECK(state.calls == 0);
    r_std_c_handle_destroy(&moved);
    R_TEST_CHECK(state.calls == 1);
    R_TEST_CHECK(state.entry_rounding == FE_TONEAREST);
    R_TEST_CHECK(state.entry_flags == 0);
    R_TEST_CHECK(state.change_failed == 0);
    R_TEST_CHECK(fegetround() == FE_DOWNWARD);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    r_std_c_handle_destroy(&moved);
    R_TEST_CHECK(state.calls == 1);
    R_TEST_CHECK(fegetround() == FE_DOWNWARD);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    handle = r_std_c_adopt_handle(&state, r_test_handle_environment_destructor);
    R_TEST_CHECK(r_std_c_release_handle(&handle) == &state);
    r_std_c_handle_destroy(&handle);
    R_TEST_CHECK(state.calls == 1);
    R_TEST_CHECK(fegetround() == FE_DOWNWARD);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == FE_DIVBYZERO);
    R_TEST_CHECK(fesetenv(&original) == 0);
    return 0;
}

static int r_test_metadata_and_error(void) {
    RStdCTargetInfo target = r_std_c_target();
    RStdError converted = r_std_c_string_as_error((RStdCStringError){
        R_STD_C_STRING_ERROR_INVALID_UTF8,
        19U,
        R_STD_ALLOC_ERROR_OUT_OF_MEMORY,
    });

    R_TEST_CHECK(target.pointer_bits == UINT32_C(64));
    R_TEST_CHECK(target.c_wint_available && target.c_long_double_available);
    R_TEST_CHECK(target.hosted_native_async);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_C_ABI);
    R_TEST_CHECK(converted.code == UINT32_C(2));
    R_TEST_CHECK(converted.native_code == INT64_C(0));
    return 0;
}

static int r_test_link_available(void) {
    static const RStdCLinkManifestEntry entries[] = {
        {{(const uint8_t *)"apple.dispatch", 14U}, 1},
        {{(const uint8_t *)"optional-zlib", 13U}, 0},
        {{(const uint8_t *)"system_libc", 11U}, 1},
    };
    const RStdCLinkManifestView manifest = {
        entries,
        sizeof(entries) / sizeof(entries[0]),
    };
    const RStdCLinkManifestView inaccessible = {NULL, SIZE_MAX};
    uint8_t maximum_name[255];
    uint8_t too_long_name[256];
    RStdCLinkManifestEntry maximum_entry;
    RStdCLinkManifestView maximum_manifest;

    (void)memset(maximum_name, 'a', sizeof(maximum_name));
    (void)memset(too_long_name, 'a', sizeof(too_long_name));
    maximum_entry.logical_name.data = maximum_name;
    maximum_entry.logical_name.length = sizeof(maximum_name);
    maximum_entry.available = 1;
    maximum_manifest.entries = &maximum_entry;
    maximum_manifest.count = 1U;

    R_TEST_CHECK(r_std_c_link_available(manifest, r_test_literal_view("apple.dispatch")));
    R_TEST_CHECK(r_std_c_link_available(manifest, r_test_literal_view("system_libc")));
    R_TEST_CHECK(!r_std_c_link_available(manifest, r_test_literal_view("optional-zlib")));
    R_TEST_CHECK(!r_std_c_link_available(manifest, r_test_literal_view("absent")));
    R_TEST_CHECK(!r_std_c_link_available(inaccessible, r_test_literal_view("Invalid")));
    R_TEST_CHECK(!r_std_c_link_available(inaccessible, r_test_literal_view("a..b")));
    R_TEST_CHECK(!r_std_c_link_available(inaccessible, r_test_literal_view("a_")));
    R_TEST_CHECK(!r_std_c_link_available(inaccessible, r_test_literal_view("")));
    R_TEST_CHECK(r_std_c_link_available(maximum_manifest,
                                        (RStdStringView){maximum_name, sizeof(maximum_name)}));
    R_TEST_CHECK(!r_std_c_link_available(inaccessible,
                                         (RStdStringView){too_long_name, sizeof(too_long_name)}));
    return 0;
}

static int r_test_thread_attachment(void) {
    RStdCAttachThreadResult attached = r_std_c_attach_thread();
    RStdCAttachThreadResult duplicate;
    RRuntimeStartResult started;
    RStdError converted;
    int original_rounding = fegetround();

    R_TEST_CHECK(attached.status == R_STD_C_CALL_ERROR);
    R_TEST_CHECK(attached.error == R_STD_C_RUNTIME_ERROR_RUNTIME_STOPPING);
    started = r_runtime_hosted_start(0, NULL);
    R_TEST_CHECK(started.started);
    R_TEST_CHECK(original_rounding != -1);
    R_TEST_CHECK(fesetround(FE_DOWNWARD) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);
    R_TEST_CHECK(feraiseexcept(FE_DIVBYZERO) == 0);

    attached = r_std_c_attach_thread();
    R_TEST_CHECK(attached.status == R_STD_C_CALL_SUCCESS);
    R_TEST_CHECK(fegetround() == FE_TONEAREST);
    R_TEST_CHECK(fetestexcept(FE_ALL_EXCEPT) == 0);
    duplicate = r_std_c_attach_thread();
    R_TEST_CHECK(duplicate.status == R_STD_C_CALL_ERROR);
    R_TEST_CHECK(duplicate.error == R_STD_C_RUNTIME_ERROR_RESOURCE_EXHAUSTED);

    R_TEST_CHECK(fesetround(FE_UPWARD) == 0);
    r_std_c_detach_thread(&attached.value);
    R_TEST_CHECK(!attached.value.active);
    R_TEST_CHECK(fegetround() == FE_DOWNWARD);
    R_TEST_CHECK((fetestexcept(FE_DIVBYZERO) & FE_DIVBYZERO) != 0);
    r_std_c_thread_attachment_destroy(&attached.value);
    R_TEST_CHECK(r_runtime_hosted_finish(0) == 0);
    R_TEST_CHECK(fesetround(original_rounding) == 0);
    R_TEST_CHECK(feclearexcept(FE_ALL_EXCEPT) == 0);

    converted = r_std_c_runtime_as_error(R_STD_C_RUNTIME_ERROR_FLOATING_ENVIRONMENT_UNAVAILABLE);
    R_TEST_CHECK(converted.domain == R_STD_ERROR_DOMAIN_C_ABI);
    R_TEST_CHECK(converted.code == UINT32_C(0x0102));
    R_TEST_CHECK(converted.native_code == INT64_C(0));
    return 0;
}

int main(void) {
    R_TEST_CHECK(r_test_c_strings() == 0);
    R_TEST_CHECK(r_test_handle() == 0);
    R_TEST_CHECK(r_test_handle_environment() == 0);
    R_TEST_CHECK(r_test_metadata_and_error() == 0);
    R_TEST_CHECK(r_test_link_available() == 0);
    R_TEST_CHECK(r_test_thread_attachment() == 0);
    (void)fprintf(stdout, "library_c_tests: ok\n");
    return 0;
}
