#include "r_std_arc.h"
#include "r_std_rc.h"

#include "r_runtime_allocator.h"
#include "r_runtime_type.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct TrackedValue {
    int value;
    int *drop_count;
} TrackedValue;

static void fail(const char *message) {
    (void)fprintf(stderr, "library shared-owner test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static void tracked_move(void *destination, void *source) {
    TrackedValue *destination_value = destination;
    TrackedValue *source_value = source;

    *destination_value = *source_value;
    source_value->drop_count = NULL;
}

static void tracked_drop(void *value) {
    TrackedValue *tracked = value;

    if (tracked->drop_count != NULL) {
        *tracked->drop_count += 1;
        tracked->drop_count = NULL;
    }
}

static RRuntimeTypeInfo tracked_type(void) {
    RRuntimeTypeInfo type = {
        sizeof(TrackedValue),
        _Alignof(TrackedValue),
        tracked_move,
        tracked_drop,
    };

    return type;
}

static RRuntimeArc create_arc(RRuntimeAllocator *allocator, int value, int *drop_count) {
    TrackedValue staged = {value, drop_count};
    RRuntimeArc owner = {0};

    require(r_runtime_arc_create(allocator, tracked_type(), &staged, &owner) == R_RUNTIME_ARC_OK,
            "arc create");
    require(staged.drop_count == NULL, "arc create consumes staged value");
    return owner;
}

static RRuntimeRc create_rc(RRuntimeAllocator *allocator, int value, int *drop_count) {
    TrackedValue staged = {value, drop_count};
    RRuntimeRc owner = {0};

    require(r_runtime_rc_create(allocator, tracked_type(), &staged, &owner) == R_RUNTIME_RC_OK,
            "rc create");
    require(staged.drop_count == NULL, "rc create consumes staged value");
    return owner;
}

static void test_empty_release_sentinels(void) {
    RRuntimeArc arc_owner = {0};
    RRuntimeWeakArc weak_arc_owner = {0};
    RRuntimeRc rc_owner = {0};
    RRuntimeWeakRc weak_rc_owner = {0};

    r_runtime_arc_release(&arc_owner);
    r_runtime_weak_arc_release(&weak_arc_owner);
    r_runtime_rc_release(&rc_owner);
    r_runtime_weak_rc_release(&weak_rc_owner);
}

static void test_shared_owner_creation_failures(void) {
    RRuntimeAllocator allocator;
    RRuntimeTypeInfo oversized = {SIZE_MAX, 1U, NULL, NULL};
    RRuntimeTypeInfo unsupported_alignment = {
        1U,
        R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT * 2U,
        NULL,
        NULL,
    };
    uint8_t staged = 0U;
    RRuntimeArc arc_owner = {0};
    RRuntimeRc rc_owner = {0};

    r_runtime_allocator_initialize(&allocator);
    require(r_runtime_arc_create(&allocator, oversized, &staged, &arc_owner) ==
                R_RUNTIME_ARC_SIZE_OVERFLOW,
            "arc size overflow remains recoverable");
    require(r_runtime_rc_create(&allocator, oversized, &staged, &rc_owner) ==
                R_RUNTIME_RC_SIZE_OVERFLOW,
            "rc size overflow remains recoverable");
    require(r_runtime_arc_create(&allocator, unsupported_alignment, &staged, &arc_owner) ==
                R_RUNTIME_ARC_UNSUPPORTED_ALIGNMENT,
            "arc unsupported alignment remains recoverable");
    require(r_runtime_rc_create(&allocator, unsupported_alignment, &staged, &rc_owner) ==
                R_RUNTIME_RC_UNSUPPORTED_ALIGNMENT,
            "rc unsupported alignment remains recoverable");
    require(arc_owner.control == NULL && rc_owner.control == NULL,
            "failed shared-owner creation remains transactional");
}

static void test_arc_observation_and_uniqueness(RRuntimeAllocator *allocator) {
    int drops = 0;
    RRuntimeArc owner = create_arc(allocator, 17, &drops);
    RRuntimeArc clone = {0};
    RRuntimeWeakArc weak = {0};
    TrackedValue *mutable_value;

    require(r_std_arc_strong_count(&owner) == 1U, "arc initial strong count");
    require(r_std_arc_weak_count(&owner) == 0U, "arc initial weak count");
    require(r_std_arc_clone(&owner, &clone) == R_STD_ARC_CALL_SUCCESS, "arc clone");
    require(r_std_arc_ptr_eq(&owner, &clone), "arc pointer identity");
    require(r_std_arc_strong_count(&owner) == 2U, "arc cloned strong count");
    require(r_std_arc_get_mut(&owner) == NULL, "arc get_mut rejects strong clone");
    r_runtime_arc_release(&clone);

    require(r_std_arc_downgrade(&owner, &weak) == R_STD_ARC_CALL_SUCCESS, "arc downgrade");
    require(r_std_arc_weak_count(&owner) == 1U, "arc explicit weak count");
    require(r_std_arc_get_mut(&owner) == NULL, "arc get_mut rejects explicit weak");
    r_runtime_weak_arc_release(&weak);

    mutable_value = r_std_arc_get_mut(&owner);
    require(mutable_value != NULL, "arc get_mut unique");
    mutable_value->value = 23;
    require(((const TrackedValue *)r_runtime_arc_get(&owner))->value == 23, "arc unique mutation");
    r_runtime_arc_release(&owner);
    require(drops == 1, "arc unique value dropped once");
}

static void test_arc_weak_raw_and_unwrap(RRuntimeAllocator *allocator) {
    int weak_drops = 0;
    int unwrap_drops = 0;
    RRuntimeArc weak_owner = create_arc(allocator, 31, &weak_drops);
    RRuntimeWeakArc weak = {0};
    RRuntimeWeakArc weak_clone = {0};
    RStdArcUpgradeResult upgrade;
    RRuntimeArc owner = create_arc(allocator, 41, &unwrap_drops);
    RRuntimeArc clone = {0};
    RRuntimeArc raw_owner = {0};
    RStdArcTryUnwrapResult unwrap;
    TrackedValue value = {0};
    const void *raw;

    require(r_std_arc_downgrade(&weak_owner, &weak) == R_STD_ARC_CALL_SUCCESS, "arc weak setup");
    require(r_std_arc_clone_weak(&weak, &weak_clone) == R_STD_ARC_CALL_SUCCESS, "arc weak clone");
    upgrade = r_std_arc_upgrade(&weak);
    require((upgrade.status == R_STD_ARC_CALL_SUCCESS) && upgrade.has_value,
            "arc weak upgrade present");
    r_runtime_arc_release(&upgrade.value);
    r_runtime_arc_release(&weak_owner);
    require(weak_drops == 1, "arc expires value once");
    upgrade = r_std_arc_upgrade(&weak);
    require((upgrade.status == R_STD_ARC_CALL_SUCCESS) && !upgrade.has_value,
            "arc weak upgrade absent");
    r_runtime_weak_arc_release(&weak_clone);
    r_runtime_weak_arc_release(&weak);

    require(r_std_arc_clone(&owner, &clone) == R_STD_ARC_CALL_SUCCESS, "arc unwrap clone");
    unwrap = r_std_arc_try_unwrap(&owner, &value);
    require((unwrap.status == R_STD_ARC_CALL_SUCCESS) &&
                (unwrap.kind == R_STD_ARC_TRY_UNWRAP_OWNER) && (owner.control == NULL),
            "arc failed unwrap returns consumed owner");
    r_runtime_arc_release(&clone);
    unwrap = r_std_arc_try_unwrap(&unwrap.owner, &value);
    require((unwrap.status == R_STD_ARC_CALL_SUCCESS) &&
                (unwrap.kind == R_STD_ARC_TRY_UNWRAP_VALUE) && (value.value == 41),
            "arc successful unwrap returns value");
    require(unwrap_drops == 0, "arc unwrap does not drop moved value");
    tracked_drop(&value);
    require(unwrap_drops == 1, "arc unwrapped value dropped once");

    raw_owner = create_arc(allocator, 53, &unwrap_drops);
    raw = r_std_arc_into_raw(&raw_owner);
    require((raw != NULL) && (raw_owner.control == NULL), "arc into_raw consumes owner");
    require(r_std_arc_from_raw(raw, &raw_owner) == R_STD_ARC_CALL_SUCCESS,
            "arc from_raw restores owner");
    r_runtime_arc_release(&raw_owner);
    r_runtime_arc_release(&raw_owner);
    require(unwrap_drops == 2, "arc raw round trip preserves one duty");
}

static void test_rc_operations(RRuntimeAllocator *allocator) {
    int drops = 0;
    RRuntimeRc owner = create_rc(allocator, 67, &drops);
    RRuntimeRc clone = {0};
    RRuntimeWeakRc weak = {0};
    RRuntimeWeakRc weak_clone = {0};
    RStdRcUpgradeResult upgrade;
    RStdRcTryUnwrapResult unwrap;
    TrackedValue value = {0};
    const void *raw;

    require(r_std_rc_clone(&owner, &clone) == R_STD_RC_CALL_SUCCESS, "rc clone");
    require(r_std_rc_ptr_eq(&owner, &clone), "rc pointer identity");
    require(r_std_rc_strong_count(&owner) == 2U, "rc strong count");
    require(r_std_rc_get_mut(&owner) == NULL, "rc get_mut rejects clone");
    require(r_std_rc_downgrade(&owner, &weak) == R_STD_RC_CALL_SUCCESS, "rc downgrade");
    require(r_std_rc_clone_weak(&weak, &weak_clone) == R_STD_RC_CALL_SUCCESS, "rc weak clone");
    require(r_std_rc_weak_count(&owner) == 2U, "rc weak count");
    upgrade = r_std_rc_upgrade(&weak);
    require((upgrade.status == R_STD_RC_CALL_SUCCESS) && upgrade.has_value, "rc upgrade present");
    r_runtime_rc_release(&upgrade.value);
    r_runtime_rc_release(&clone);
    r_runtime_weak_rc_release(&weak_clone);
    r_runtime_weak_rc_release(&weak);
    require(((TrackedValue *)r_std_rc_get_mut(&owner))->value == 67, "rc get_mut unique");

    raw = r_std_rc_into_raw(&owner);
    require((raw != NULL) && (owner.control == NULL), "rc into_raw consumes owner");
    require(r_std_rc_from_raw(raw, &owner) == R_STD_RC_CALL_SUCCESS, "rc from_raw restores owner");
    unwrap = r_std_rc_try_unwrap(&owner, &value);
    require((unwrap.status == R_STD_RC_CALL_SUCCESS) &&
                (unwrap.kind == R_STD_RC_TRY_UNWRAP_VALUE) && (value.value == 67),
            "rc unwrap value");
    require(drops == 0, "rc unwrap moves without drop");
    tracked_drop(&value);
    require(drops == 1, "rc moved value dropped once");
    r_runtime_rc_release(&owner);
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    test_empty_release_sentinels();
    test_shared_owner_creation_failures();
    test_arc_observation_and_uniqueness(&allocator);
    test_arc_weak_raw_and_unwrap(&allocator);
    test_rc_operations(&allocator);
    (void)fprintf(stdout, "library_shared_owner_tests: ok\n");
    return EXIT_SUCCESS;
}
