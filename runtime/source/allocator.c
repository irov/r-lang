#include "r_runtime_allocator.h"

#include "r_runtime_budget.h"
#include "r_runtime_target_abi.h"

#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

_Static_assert(R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE == R_RUNTIME_TARGET_MAXIMUM_OBJECT_SIZE,
               "runtime allocator maximum object size differs from target manifest");

static _Bool r_runtime_allocator_should_fail(RRuntimeAllocator *allocator) {
    const uint64_t attempt =
        atomic_fetch_add_explicit(&allocator->attempt_count, UINT64_C(1), memory_order_relaxed) +
        UINT64_C(1);
    const uint64_t fail_at =
        atomic_load_explicit(&allocator->fail_at_attempt, memory_order_relaxed);
    return (fail_at != UINT64_C(0)) && (attempt == fail_at);
}

static RRuntimeAllocationStatus
r_runtime_allocator_allocate_aligned(size_t size, size_t alignment, void **result) {
    void *base;
    uintptr_t raw;
    uintptr_t aligned;
    size_t total;

    if (alignment <= _Alignof(max_align_t)) {
        *result = malloc(size);
        return *result == NULL ? R_RUNTIME_ALLOCATION_EXHAUSTED : R_RUNTIME_ALLOCATION_OK;
    }
    if (size > (SIZE_MAX - (alignment - 1U) - sizeof(void *))) {
        return R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    }
    total = size + (alignment - 1U) + sizeof(void *);
    base = malloc(total);
    if (base == NULL) {
        return R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    raw = (uintptr_t)base + (uintptr_t)sizeof(void *);
    aligned = (raw + (uintptr_t)(alignment - 1U)) & ~(uintptr_t)(alignment - 1U);
    ((void **)aligned)[-1] = base;
    *result = (void *)aligned;
    return R_RUNTIME_ALLOCATION_OK;
}

/* ---- Budgets (Core R-STMT-0020) ---- */

struct RRuntimeBudget {
    _Atomic uint64_t bytes_used;
    _Atomic uint64_t tasks_used;
    uint64_t bytes_limit;
    uint64_t tasks_limit;
    RRuntimeBudget *parent;
    _Atomic size_t references;
    _Bool refusing;
};

static RRuntimeBudget r_runtime_budget_refusing_value = {
    0U, 0U, UINT64_C(0), UINT64_C(0), NULL, 1U, 1};

static _Thread_local RRuntimeBudget *r_runtime_budget_current_value;
static _Thread_local _Bool r_runtime_budget_refused;

/* The allocations charged to budgets, keyed by their address, in shards each guarded by a spin
 * lock. The table is kept with the C allocator, outside every budget. Slots hold key 0 when
 * empty and key 1 when removed. */
#define R_RUNTIME_BUDGET_SHARDS 64U
#define R_RUNTIME_BUDGET_EMPTY ((uintptr_t)0U)
#define R_RUNTIME_BUDGET_REMOVED ((uintptr_t)1U)

typedef struct RRuntimeBudgetSlot {
    uintptr_t key;
    RRuntimeBudget *budget;
    size_t size;
} RRuntimeBudgetSlot;

typedef struct RRuntimeBudgetShard {
    atomic_flag lock;
    RRuntimeBudgetSlot *slots;
    size_t capacity;
    size_t used;
    size_t removed;
} RRuntimeBudgetShard;

static RRuntimeBudgetShard r_runtime_budget_shards[R_RUNTIME_BUDGET_SHARDS] = {
#define R_RUNTIME_BUDGET_SHARD_INIT {ATOMIC_FLAG_INIT, NULL, 0U, 0U, 0U}
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT, R_RUNTIME_BUDGET_SHARD_INIT,
    R_RUNTIME_BUDGET_SHARD_INIT,
#undef R_RUNTIME_BUDGET_SHARD_INIT
};

/* The number of charged allocations; while it is zero a release looks up nothing. */
static _Atomic size_t r_runtime_budget_charged;

static uint64_t r_runtime_budget_hash(uintptr_t key) {
    return ((uint64_t)key >> 4U) * UINT64_C(0x9E3779B97F4A7C15);
}

static RRuntimeBudgetShard *r_runtime_budget_shard(uint64_t hash) {
    return &r_runtime_budget_shards[(size_t)(hash >> 58U)];
}

static void r_runtime_budget_lock(RRuntimeBudgetShard *shard) {
    unsigned spins = 0U;

    while (atomic_flag_test_and_set_explicit(&shard->lock, memory_order_acquire)) {
        spins += 1U;
        if (spins >= 64U) {
            spins = 0U;
            (void)sched_yield();
        }
    }
}

static void r_runtime_budget_unlock(RRuntimeBudgetShard *shard) {
    atomic_flag_clear_explicit(&shard->lock, memory_order_release);
}

/* Rehashes a shard into the given power-of-two capacity; false when it cannot be allocated. */
static _Bool r_runtime_budget_rehash(RRuntimeBudgetShard *shard, size_t capacity) {
    RRuntimeBudgetSlot *slots = calloc(capacity, sizeof(RRuntimeBudgetSlot));

    if (slots == NULL) {
        return 0;
    }
    for (size_t index = 0U; index < shard->capacity; ++index) {
        const RRuntimeBudgetSlot *slot = &shard->slots[index];
        size_t probe;

        if (slot->key <= R_RUNTIME_BUDGET_REMOVED) {
            continue;
        }
        probe = (size_t)r_runtime_budget_hash(slot->key) & (capacity - 1U);
        while (slots[probe].key != R_RUNTIME_BUDGET_EMPTY) {
            probe = (probe + 1U) & (capacity - 1U);
        }
        slots[probe] = *slot;
    }
    free(shard->slots);
    shard->slots = slots;
    shard->capacity = capacity;
    shard->removed = 0U;
    return 1;
}

static _Bool r_runtime_budget_record(void *allocation, RRuntimeBudget *budget, size_t size) {
    const uintptr_t key = (uintptr_t)allocation;
    const uint64_t hash = r_runtime_budget_hash(key);
    RRuntimeBudgetShard *shard = r_runtime_budget_shard(hash);
    size_t probe;

    r_runtime_budget_lock(shard);
    if ((shard->used + shard->removed + 1U) * 4U > shard->capacity * 3U) {
        size_t capacity = shard->capacity == 0U ? 64U : shard->capacity;

        while ((shard->used + 1U) * 2U > capacity) {
            capacity *= 2U;
        }
        if (!r_runtime_budget_rehash(shard, capacity)) {
            r_runtime_budget_unlock(shard);
            return 0;
        }
    }
    probe = (size_t)hash & (shard->capacity - 1U);
    while (shard->slots[probe].key > R_RUNTIME_BUDGET_REMOVED) {
        probe = (probe + 1U) & (shard->capacity - 1U);
    }
    if (shard->slots[probe].key == R_RUNTIME_BUDGET_REMOVED) {
        shard->removed -= 1U;
    }
    shard->slots[probe].key = key;
    shard->slots[probe].budget = budget;
    shard->slots[probe].size = size;
    shard->used += 1U;
    r_runtime_budget_unlock(shard);
    (void)atomic_fetch_add_explicit(&r_runtime_budget_charged, 1U, memory_order_relaxed);
    return 1;
}

/* Removes the record of a charged allocation; false when it was charged to no budget. */
static _Bool r_runtime_budget_take(const void *allocation, RRuntimeBudget **budget, size_t *size) {
    const uintptr_t key = (uintptr_t)allocation;
    const uint64_t hash = r_runtime_budget_hash(key);
    RRuntimeBudgetShard *shard;
    size_t probe;
    _Bool found = 0;

    if (atomic_load_explicit(&r_runtime_budget_charged, memory_order_relaxed) == 0U) {
        return 0;
    }
    shard = r_runtime_budget_shard(hash);
    r_runtime_budget_lock(shard);
    if (shard->capacity != 0U) {
        probe = (size_t)hash & (shard->capacity - 1U);
        while (shard->slots[probe].key != R_RUNTIME_BUDGET_EMPTY) {
            if (shard->slots[probe].key == key) {
                *budget = shard->slots[probe].budget;
                *size = shard->slots[probe].size;
                shard->slots[probe].key = R_RUNTIME_BUDGET_REMOVED;
                shard->slots[probe].budget = NULL;
                shard->used -= 1U;
                shard->removed += 1U;
                found = 1;
                break;
            }
            probe = (probe + 1U) & (shard->capacity - 1U);
        }
    }
    r_runtime_budget_unlock(shard);
    if (found) {
        (void)atomic_fetch_sub_explicit(&r_runtime_budget_charged, 1U, memory_order_relaxed);
    }
    return found;
}

/* Adds amount to the counter of every budget of the chain within its limit; false, with every
 * addition undone, when one would exceed its limit. */
static _Bool r_runtime_budget_add(RRuntimeBudget *budget, uint64_t amount, _Bool tasks) {
    for (RRuntimeBudget *cursor = budget; cursor != NULL; cursor = cursor->parent) {
        _Atomic uint64_t *counter = tasks ? &cursor->tasks_used : &cursor->bytes_used;
        const uint64_t limit = tasks ? cursor->tasks_limit : cursor->bytes_limit;
        uint64_t used = atomic_load_explicit(counter, memory_order_relaxed);
        _Bool admitted;

        do {
            admitted = (used <= limit) && (amount <= limit - used);
        } while (admitted &&
                 !atomic_compare_exchange_weak_explicit(
                     counter, &used, used + amount, memory_order_relaxed, memory_order_relaxed));
        if (!admitted) {
            for (RRuntimeBudget *undo = budget; undo != cursor; undo = undo->parent) {
                (void)atomic_fetch_sub_explicit(
                    tasks ? &undo->tasks_used : &undo->bytes_used, amount, memory_order_relaxed);
            }
            return 0;
        }
    }
    return 1;
}

static void r_runtime_budget_subtract(RRuntimeBudget *budget, uint64_t amount, _Bool tasks) {
    for (RRuntimeBudget *cursor = budget; cursor != NULL; cursor = cursor->parent) {
        (void)atomic_fetch_sub_explicit(
            tasks ? &cursor->tasks_used : &cursor->bytes_used, amount, memory_order_relaxed);
    }
}

RRuntimeBudget *r_runtime_budget_create(
    RRuntimeBudget *parent, _Bool has_bytes, uint64_t bytes, _Bool has_tasks, uint64_t tasks) {
    RRuntimeBudget *budget = malloc(sizeof(RRuntimeBudget));

    if (budget == NULL) {
        return NULL;
    }
    atomic_init(&budget->bytes_used, UINT64_C(0));
    atomic_init(&budget->tasks_used, UINT64_C(0));
    budget->bytes_limit = has_bytes ? bytes : UINT64_MAX;
    budget->tasks_limit = has_tasks ? tasks : UINT64_MAX;
    budget->parent = parent;
    atomic_init(&budget->references, (size_t)1U);
    budget->refusing = 0;
    if (parent != NULL) {
        r_runtime_budget_retain(parent);
    }
    return budget;
}

RRuntimeBudget *r_runtime_budget_refusing(void) {
    return &r_runtime_budget_refusing_value;
}

void r_runtime_budget_retain(RRuntimeBudget *budget) {
    if (budget != NULL && !budget->refusing) {
        (void)atomic_fetch_add_explicit(&budget->references, 1U, memory_order_relaxed);
    }
}

void r_runtime_budget_release(RRuntimeBudget *budget) {
    while (budget != NULL && !budget->refusing &&
           atomic_fetch_sub_explicit(&budget->references, 1U, memory_order_acq_rel) == 1U) {
        RRuntimeBudget *parent = budget->parent;

        free(budget);
        budget = parent;
    }
}

RRuntimeBudget *r_runtime_budget_current(void) {
    return r_runtime_budget_current_value;
}

RRuntimeBudget *r_runtime_budget_swap_current(RRuntimeBudget *budget) {
    RRuntimeBudget *previous = r_runtime_budget_current_value;

    r_runtime_budget_current_value = budget;
    return previous;
}

_Bool r_runtime_budget_charge_task(RRuntimeBudget *budget) {
    if (budget == NULL) {
        return 1;
    }
    if (!r_runtime_budget_add(budget, UINT64_C(1), 1)) {
        r_runtime_budget_refused = 1;
        return 0;
    }
    return 1;
}

void r_runtime_budget_return_task(RRuntimeBudget *budget) {
    if (budget != NULL) {
        r_runtime_budget_subtract(budget, UINT64_C(1), 1);
    }
}

_Bool r_runtime_allocation_refused_by_budget(void) {
    return r_runtime_budget_refused;
}

void r_runtime_budget_note_refusal(void) {
    r_runtime_budget_refused = 1;
}

uint64_t r_runtime_budget_bytes_used(const RRuntimeBudget *budget) {
    return budget == NULL ? UINT64_C(0)
                          : atomic_load_explicit(&budget->bytes_used, memory_order_relaxed);
}

uint64_t r_runtime_budget_tasks_used(const RRuntimeBudget *budget) {
    return budget == NULL ? UINT64_C(0)
                          : atomic_load_explicit(&budget->tasks_used, memory_order_relaxed);
}

uint64_t r_runtime_budget_bytes_limit(const RRuntimeBudget *budget) {
    return budget == NULL ? UINT64_MAX : budget->bytes_limit;
}

uint64_t r_runtime_budget_tasks_limit(const RRuntimeBudget *budget) {
    return budget == NULL ? UINT64_MAX : budget->tasks_limit;
}

uint64_t r_runtime_budget_bytes_available(const RRuntimeBudget *budget) {
    uint64_t available = UINT64_MAX;

    for (const RRuntimeBudget *cursor = budget; cursor != NULL; cursor = cursor->parent) {
        const uint64_t used = atomic_load_explicit(&cursor->bytes_used, memory_order_relaxed);
        uint64_t room;

        if (cursor->bytes_limit == UINT64_MAX) {
            continue;
        }
        room = used < cursor->bytes_limit ? cursor->bytes_limit - used : UINT64_C(0);
        if (room < available) {
            available = room;
        }
    }
    return available;
}

/* Frees storage of the C allocator without touching the budgets. */
static void r_runtime_allocator_release_storage(void *allocation, size_t alignment) {
    if (alignment <= _Alignof(max_align_t)) {
        free(allocation);
    } else {
        free(((void **)allocation)[-1]);
    }
}

void r_runtime_allocator_initialize(RRuntimeAllocator *allocator) {
    atomic_init(&allocator->attempt_count, UINT64_C(0));
    atomic_init(&allocator->fail_at_attempt, UINT64_C(0));
}

void r_runtime_allocator_set_failure(RRuntimeAllocator *allocator, uint64_t attempt) {
    atomic_store_explicit(&allocator->attempt_count, UINT64_C(0), memory_order_relaxed);
    atomic_store_explicit(&allocator->fail_at_attempt, attempt, memory_order_relaxed);
}

uint64_t r_runtime_allocator_attempt_count(const RRuntimeAllocator *allocator) {
    return atomic_load_explicit(&allocator->attempt_count, memory_order_relaxed);
}

RRuntimeAllocationStatus r_runtime_allocator_allocate(RRuntimeAllocator *allocator,
                                                      size_t size,
                                                      size_t alignment,
                                                      void **result) {
    *result = NULL;
    if (size > R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE) {
        return R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    }
    if (alignment > R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT) {
        return R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT;
    }
    if (size == 0U) {
        return R_RUNTIME_ALLOCATION_OK;
    }
    r_runtime_budget_refused = 0;
    if (r_runtime_allocator_should_fail(allocator)) {
        return R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    {
        RRuntimeBudget *budget = r_runtime_budget_current_value;
        RRuntimeAllocationStatus status;

        if (budget == NULL) {
            return r_runtime_allocator_allocate_aligned(size, alignment, result);
        }
        /* Core R-STMT-0020: the budget of the executing task and its ancestors pay first. */
        if (!r_runtime_budget_add(budget, (uint64_t)size, 0)) {
            r_runtime_budget_refused = 1;
            return R_RUNTIME_ALLOCATION_EXHAUSTED;
        }
        status = r_runtime_allocator_allocate_aligned(size, alignment, result);
        if (status != R_RUNTIME_ALLOCATION_OK) {
            r_runtime_budget_subtract(budget, (uint64_t)size, 0);
            return status;
        }
        if (budget->refusing || !r_runtime_budget_record(*result, budget, size)) {
            r_runtime_allocator_release_storage(*result, alignment);
            r_runtime_budget_subtract(budget, (uint64_t)size, 0);
            *result = NULL;
            return R_RUNTIME_ALLOCATION_EXHAUSTED;
        }
        r_runtime_budget_retain(budget);
        return R_RUNTIME_ALLOCATION_OK;
    }
}

RRuntimeAllocationStatus r_runtime_allocator_reallocate(RRuntimeAllocator *allocator,
                                                        void *allocation,
                                                        size_t old_size,
                                                        size_t new_size,
                                                        size_t alignment,
                                                        void **result) {
    void *replacement;

    *result = allocation;
    if (new_size > R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE) {
        return R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    }
    if (alignment > R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT) {
        return R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT;
    }
    if (new_size == 0U) {
        r_runtime_allocator_deallocate(allocation, alignment);
        *result = NULL;
        return R_RUNTIME_ALLOCATION_OK;
    }
    if (allocation == NULL) {
        return r_runtime_allocator_allocate(allocator, new_size, alignment, result);
    }
    r_runtime_budget_refused = 0;
    if (r_runtime_allocator_should_fail(allocator)) {
        return R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    {
        RRuntimeBudget *budget = NULL;
        size_t charged = 0U;
        const _Bool recorded = r_runtime_budget_take(allocation, &budget, &charged);

        /* Core R-STMT-0020: a charged allocation stays with its budget, which pays the growth
           and receives the shrinkage; an uncharged one stays uncharged. */
        if (recorded && new_size > charged &&
            !r_runtime_budget_add(budget, (uint64_t)(new_size - charged), 0)) {
            r_runtime_budget_refused = 1;
            if (!r_runtime_budget_record(allocation, budget, charged)) {
                r_runtime_budget_subtract(budget, (uint64_t)charged, 0);
                r_runtime_budget_release(budget);
            }
            return R_RUNTIME_ALLOCATION_EXHAUSTED;
        }
        if (alignment <= _Alignof(max_align_t)) {
            replacement = realloc(allocation, new_size);
        } else if (r_runtime_allocator_allocate_aligned(new_size, alignment, &replacement) !=
                   R_RUNTIME_ALLOCATION_OK) {
            replacement = NULL;
        } else {
            (void)memcpy(replacement, allocation, old_size < new_size ? old_size : new_size);
            r_runtime_allocator_release_storage(allocation, alignment);
        }
        if (replacement == NULL) {
            if (recorded) {
                if (new_size > charged) {
                    r_runtime_budget_subtract(budget, (uint64_t)(new_size - charged), 0);
                }
                if (!r_runtime_budget_record(allocation, budget, charged)) {
                    r_runtime_budget_subtract(budget, (uint64_t)charged, 0);
                    r_runtime_budget_release(budget);
                }
            }
            return R_RUNTIME_ALLOCATION_EXHAUSTED;
        }
        if (recorded) {
            if (new_size < charged) {
                r_runtime_budget_subtract(budget, (uint64_t)(charged - new_size), 0);
            }
            /* Without room for the new record the allocation leaves its budget. */
            if (!r_runtime_budget_record(replacement, budget, new_size)) {
                r_runtime_budget_subtract(budget, (uint64_t)new_size, 0);
                r_runtime_budget_release(budget);
            }
        }
        *result = replacement;
        return R_RUNTIME_ALLOCATION_OK;
    }
}

void r_runtime_allocator_deallocate(void *allocation, size_t alignment) {
    RRuntimeBudget *budget = NULL;
    size_t charged = 0U;

    if (allocation == NULL) {
        return;
    }
    if (r_runtime_budget_take(allocation, &budget, &charged)) {
        r_runtime_budget_subtract(budget, (uint64_t)charged, 0);
        r_runtime_budget_release(budget);
    }
    r_runtime_allocator_release_storage(allocation, alignment);
}
