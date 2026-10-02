module test.codegen.checked_finally_control;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

error cleanup_error {
    i32 code;
};

error work_error {
    i32 code;
};

protected void fail_cleanup() throws cleanup_error {
    throw {
        .code = 9,
    };
}

protected void fail_work() throws work_error {
    throw {
        .code = 17,
    };
}

protected i32 nested_return(i32* trace) {
    try {
        try {
            return 7;
        } finally {
            *trace = (*trace * 10) + 1;
        }
    } finally {
        *trace = (*trace * 10) + 2;
    }
}

protected i32 return_through_caught_cleanup(i32* cleanup_code) {
    try {
        return 11;
    } finally {
        try {
            fail_cleanup();
        } catch (cleanup_error error) {
            *cleanup_code = error.code;
        }
    }
}

protected i32 return_through_nested_finally_body(i32* trace) {
    try {
        return 19;
    } finally {
        try {
            *trace = (*trace * 10) + 1;
        } finally {
            *trace = (*trace * 10) + 2;
        }
        *trace = (*trace * 10) + 3;
    }
}

protected void error_through_caught_cleanup(i32* cleanup_code) throws work_error {
    try {
        throw {
            .code = 13,
        };
    } finally {
        try {
            fail_cleanup();
        } catch (cleanup_error error) {
            *cleanup_code = error.code;
        }
    }
}

protected void automatic_error_through_finally(i32* trace) throws work_error {
    try {
        array<u8> temporary = {};
        fail_work();
        drop temporary;
    } finally {
        *trace = (*trace * 10) + 4;
    }
}

protected i32 structural_finally(i32* trace, i32 mode) throws work_error {
    try {
        if (mode == 0) {
            return 5;
        }
        if (mode == 1) {
            fail_work();
        }
        *trace += 1;
    } finally {
        *trace = 314159;
    }
    return 6;
}

protected void loop_transfers(i32* finally_count) {
    i32 index = 0;
    while (index < 3) {
        index += 1;
        try {
            own i32* transfer_guard = new i32(index);
            if (index == 1) {
                continue;
            }
            if (index == 2) {
                break;
            }
            drop transfer_guard;
        } finally {
            *finally_count += 1;
        }
    }
}

protected void direct_loop_transfers() {
    i32 index = 0;
    while (index < 3) {
        index += 1;
        own i32* transfer_guard = new i32(index);
        if (index == 1) {
            continue;
        }
        if (index == 2) {
            break;
        }
        drop transfer_guard;
    }
}

protected void normal_completion(i32* trace) {
    try {
        *trace += 1;
    } finally {
        *trace *= 10;
    }
}

protected i32 return_through_internal_finally_loop(i32* trace) {
    try {
        return 17;
    } finally {
        i32 index = 0;
        while (index < 4) {
            index += 1;
            if (index == 1) {
                continue;
            }
            *trace = (*trace * 10) + index;
            if (index == 3) {
                break;
            }
        }
    }
}

protected void fallthrough_after_finally(i32* trace) {
    switch (0) {
        case 0:
            try {
                *trace = (*trace * 10) + 1;
            } finally {
                *trace = (*trace * 10) + 2;
            }
            fallthrough;
        default:
            *trace = (*trace * 10) + 3;
            break;
    }
}

i32 main() {
    try {
        i32 return_trace = 0;
        i32 value = nested_return(&return_trace);
        if ((value != 7) || (return_trace != 12)) {
            throw TestAssertionFailed {.code = 1};
        }

        i32 transfer_finally_count = 0;
        loop_transfers(&transfer_finally_count);
        if (transfer_finally_count != 2) {
            throw TestAssertionFailed {.code = 2};
        }
        direct_loop_transfers();

        i32 nested_body_trace = 0;
        i32 nested_body_value = return_through_nested_finally_body(&nested_body_trace);
        if ((nested_body_value != 19) || (nested_body_trace != 123)) {
            throw TestAssertionFailed {.code = 17};
        }

        i32 normal_trace = 0;
        normal_completion(&normal_trace);
        if (normal_trace != 10) {
            throw TestAssertionFailed {.code = 3};
        }

        i32 internal_loop_trace = 0;
        i32 internal_loop_value = return_through_internal_finally_loop(&internal_loop_trace);
        if ((internal_loop_value != 17) || (internal_loop_trace != 23)) {
            throw TestAssertionFailed {.code = 8};
        }

        i32 fallthrough_trace = 0;
        fallthrough_after_finally(&fallthrough_trace);
        if (fallthrough_trace != 123) {
            throw TestAssertionFailed {.code = 4};
        }

        i32 return_cleanup_code = 0;
        i32 returned = return_through_caught_cleanup(&return_cleanup_code);
        if ((returned != 11) || (return_cleanup_code != 9)) {
            throw TestAssertionFailed {.code = 5};
        }

        i32 error_cleanup_code = 0;
        try {
            error_through_caught_cleanup(&error_cleanup_code);
            throw TestAssertionFailed {.code = 6};
        } catch (work_error error) {
            if ((error.code != 13) || (error_cleanup_code != 9)) {
                throw TestAssertionFailed {.code = 7};
            }
        }

        i32 automatic_trace = 0;
        try {
            automatic_error_through_finally(&automatic_trace);
            throw TestAssertionFailed {.code = 9};
        } catch (work_error error) {
            if ((error.code != 17) || (automatic_trace != 4)) {
                throw TestAssertionFailed {.code = 10};
            }
        }

        i32 structural_trace = 0;
        TestStorage1 storage_structural_return = {.value = 0};
        try {
            storage_structural_return.value = structural_finally(&structural_trace, 0);
        } catch (work_error error) {
            error as void;
            throw TestAssertionFailed {.code = 15};
        }
        if ((storage_structural_return.value != 5) || (structural_trace != 314159)) {
            throw TestAssertionFailed {.code = 11};
        }
        i32 structural_trace_2 = 0;
        try {
            storage_structural_return.value = structural_finally(&structural_trace_2, 2);
        } catch (work_error error) {
            error as void;
            throw TestAssertionFailed {.code = 16};
        }
        if ((storage_structural_return.value != 6) || (structural_trace_2 != 314159)) {
            throw TestAssertionFailed {.code = 12};
        }
        i32 structural_trace_3 = 0;
        try {
            structural_finally(&structural_trace_3, 1) as void;
            throw TestAssertionFailed {.code = 13};
        } catch (work_error error) {
            if ((error.code != 17) || (structural_trace_3 != 314159)) {
                throw TestAssertionFailed {.code = 14};
            }
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
