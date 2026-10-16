module runtime.heap;
import runtime.types;
import runtime.types::{Allocator, AllocationStatus, Budget};

// The allocator of the runtime (r_runtime_allocator.h) over the C library's allocator, with the
// budgets of Core R-STMT-0020 (r_runtime_budget.h) charged for the allocations they bound.
@header("r_runtime_allocator.h")
@header("r_runtime_budget.h")
extern "C" {
}

// The alignment the C allocator gives every allocation: that of max_align_t, which is long double
// on the target.
const usize fundamental_alignment = alignof(c_long_double);
const usize word = sizeof(raw void*?);

// ---- Budgets (Core R-STMT-0020) ----

// The record behind an RRuntimeBudget handle; C sees it as an incomplete type.
struct BudgetState {
    atomic u64 bytes_used;
    atomic u64 tasks_used;
    u64 bytes_limit;
    u64 tasks_limit;
    raw BudgetState*? parent;
    atomic usize references;
    bool refusing;
};

const u64 unlimited = 18446744073709551615u64;

// The budget of a block whose budget could not be recorded: it refuses every charge and is never
// released.
BudgetState refusing_budget = {.bytes_used = 0u64, .tasks_used = 0u64, .bytes_limit = 0u64,
                               .tasks_limit = 0u64, .parent = null, .references = 1usize,
                               .refusing = true};

thread_local raw BudgetState*? current_budget = null;
thread_local bool refused_by_budget = false;

// The allocations charged to budgets, keyed by their address, in shards each guarded by a spin
// lock. The table is kept with the C allocator, outside every budget. Slots hold key 0 when empty
// and key 1 when removed.
struct Slot {
    usize key;
    raw BudgetState*? budget;
    usize size;
};

struct Shard {
    atomic bool lock = false;
    raw Slot*? slots = null;
    usize capacity = 0usize;
    usize used = 0usize;
    usize removed = 0usize;
};

const usize empty_key = 0usize;
const usize removed_key = 1usize;
Shard[64] shards = {};

// The number of charged allocations; while it is zero a release looks up nothing.
atomic usize charged = 0usize;

@link(name = "system.libc", kind = "system")
@header("stdlib.h")
@header("sched.h")
protected extern "C" {
    @safety("R-RUNTIME-MALLOC", "None")
    raw void*? malloc(c_size size);
    @safety("R-RUNTIME-CALLOC", "None")
    raw void*? calloc(c_size count, c_size size);
    @safety("R-RUNTIME-REALLOC", "allocation is null or a live allocation of the C allocator")
    raw void*? realloc(raw void*? allocation, c_size size);
    @safety("R-RUNTIME-FREE", "allocation is null or a live allocation of the C allocator")
    void free(raw void*? allocation);
    @safety("R-RUNTIME-SCHED-YIELD", "None")
    c_int sched_yield();
}

protected raw BudgetState*? state_of(raw const Budget*? budget) {
    unsafe {
        return core::cast_mutable(budget as raw const void*?) as raw BudgetState*?;
    }
}

protected raw BudgetState*? state_of_mutable(raw Budget*? budget) {
    unsafe {
        return (budget as raw void*?) as raw BudgetState*?;
    }
}

protected raw Budget*? handle_of(raw BudgetState*? state) {
    unsafe {
        return (state as raw void*?) as raw Budget*?;
    }
}

protected bool is_null(raw BudgetState*? state) {
    unsafe {
        return state == null;
    }
}

protected bool is_absent(raw void*? memory) {
    unsafe {
        return memory == null;
    }
}

protected bool as_bool(c_bool value) {
    return value != 0i32 as c_bool;
}

protected c_bool as_c_bool(bool value) {
    if (value == true) {
        return 1i32 as c_bool;
    }
    return 0i32 as c_bool;
}

protected bool exchanged(core::atomic_compare_exchange_result<u64> result, u64* observed) {
    switch (move result) {
    case variant core::atomic_compare_exchange_result::exchanged(move value):
        *observed = value;
        return true;
    case variant core::atomic_compare_exchange_result::unchanged(move value):
        *observed = value;
        return false;
    }
}

// The byte or task counter of a budget and its limit.
protected u64 counter_load(raw BudgetState*? state, bool tasks) {
    unsafe {
        if (tasks == true) {
            return core::atomic_load(&state->tasks_used, core::memory_order::relaxed);
        }
        return core::atomic_load(&state->bytes_used, core::memory_order::relaxed);
    }
}

protected bool counter_exchange(raw BudgetState*? state, bool tasks, u64 expected, u64 desired,
                                u64* observed) {
    unsafe {
        if (tasks == true) {
            return exchanged(core::atomic_compare_exchange(&state->tasks_used, expected, desired,
                                                           core::memory_order::relaxed,
                                                           core::memory_order::relaxed),
                             observed);
        }
        return exchanged(core::atomic_compare_exchange(&state->bytes_used, expected, desired,
                                                       core::memory_order::relaxed,
                                                       core::memory_order::relaxed),
                         observed);
    }
}

protected void counter_subtract(raw BudgetState*? state, bool tasks, u64 amount) {
    unsafe {
        if (tasks == true) {
            core::atomic_fetch_sub(&state->tasks_used, amount, core::memory_order::relaxed) as void;
            return;
        }
        core::atomic_fetch_sub(&state->bytes_used, amount, core::memory_order::relaxed) as void;
    }
}

protected u64 limit_of(raw BudgetState*? state, bool tasks) {
    unsafe {
        if (tasks == true) {
            return state->tasks_limit;
        }
        return state->bytes_limit;
    }
}

protected raw BudgetState*? parent_of(raw BudgetState*? state) {
    unsafe {
        return state->parent;
    }
}

// Adds amount to the counter of every budget of the chain within its limit; false, with every
// addition undone, when one would exceed its limit.
protected bool add(raw BudgetState*? budget, u64 amount, bool tasks) {
    raw BudgetState*? cursor = budget;
    unsafe {
        while (cursor != null) {
            u64 limit = limit_of(cursor, tasks);
            u64 used = counter_load(cursor, tasks);
            bool admitted = (used <= limit) && (amount <= limit - used);
            while (admitted == true) {
                if (counter_exchange(cursor, tasks, used, used + amount, &used) == true) {
                    break;
                }
                admitted = (used <= limit) && (amount <= limit - used);
            }
            if (admitted == false) {
                raw BudgetState*? undo = budget;
                while (undo != cursor) {
                    counter_subtract(undo, tasks, amount);
                    undo = parent_of(undo);
                }
                return false;
            }
            cursor = parent_of(cursor);
        }
    }
    return true;
}

protected void subtract(raw BudgetState*? budget, u64 amount, bool tasks) {
    raw BudgetState*? cursor = budget;
    unsafe {
        while (cursor != null) {
            counter_subtract(cursor, tasks, amount);
            cursor = parent_of(cursor);
        }
    }
}

protected bool is_refusing(raw BudgetState*? state) {
    unsafe {
        return state->refusing;
    }
}

protected void retain(raw BudgetState*? budget) {
    unsafe {
        if ((budget != null) && (is_refusing(budget) == false)) {
            core::atomic_fetch_add(&budget->references, 1usize, core::memory_order::relaxed)
                as void;
        }
    }
}

protected void release(raw BudgetState*? budget) {
    raw BudgetState*? cursor = budget;
    unsafe {
        while ((cursor != null) && (is_refusing(cursor) == false)) {
            if (core::atomic_fetch_sub(&cursor->references, 1usize, core::memory_order::acq_rel) !=
                1usize) {
                return;
            }
            raw BudgetState*? parent = parent_of(cursor);
            free(cursor as raw void*?);
            cursor = parent;
        }
    }
}

// ---- The table of charged allocations ----

protected u64 hash_of(usize key) {
    return core::wrapping_mul_u64((key >> 4usize) as u64, 0x9E3779B97F4A7C15u64);
}

protected raw Shard* shard_of(u64 hash) {
    unsafe {
        return &shards[(hash >> 58u64) as usize] as raw Shard*;
    }
}

protected raw Slot*? slot_at(raw Slot*? slots, usize index) {
    unsafe {
        return core::offset_bytes(slots, index * sizeof(Slot)) as raw Slot*?;
    }
}

protected void lock(raw Shard* shard) {
    u32 spins = 0u32;
    unsafe {
        while (core::atomic_exchange(&shard->lock, true, core::memory_order::acquire) == true) {
            spins += 1u32;
            if (spins >= 64u32) {
                spins = 0u32;
                sched_yield() as void;
            }
        }
    }
}

protected void unlock(raw Shard* shard) {
    unsafe {
        core::atomic_store(&shard->lock, false, core::memory_order::release);
    }
}

// Rehashes a shard into the given power-of-two capacity; false when it cannot be allocated.
protected bool rehash(raw Shard* shard, usize capacity) {
    unsafe {
        raw Slot*? slots = calloc(capacity as c_size, sizeof(Slot) as c_size) as raw Slot*?;
        if (slots == null) {
            return false;
        }
        usize mask = capacity - 1usize;
        for (usize index = 0usize; index < shard->capacity; index += 1usize) {
            raw Slot*? slot = slot_at(shard->slots, index);
            if (slot->key > removed_key) {
                usize probe = (hash_of(slot->key) as usize) & mask;
                raw Slot*? target = slot_at(slots, probe);
                while (target->key != empty_key) {
                    probe = (probe + 1usize) & mask;
                    target = slot_at(slots, probe);
                }
                *target = *slot;
            }
        }
        free(shard->slots as raw void*?);
        shard->slots = slots;
        shard->capacity = capacity;
        shard->removed = 0usize;
    }
    return true;
}

protected bool record(raw void*? allocation, raw BudgetState*? budget, usize size) {
    usize key = core::pointer_address(allocation);
    u64 hash = hash_of(key);
    raw Shard* shard = shard_of(hash);

    lock(shard);
    unsafe {
        if ((shard->used + shard->removed + 1usize) * 4usize > shard->capacity * 3usize) {
            usize capacity = 64usize;
            if (shard->capacity != 0usize) {
                capacity = shard->capacity;
            }
            while ((shard->used + 1usize) * 2usize > capacity) {
                capacity *= 2usize;
            }
            if (rehash(shard, capacity) == false) {
                unlock(shard);
                return false;
            }
        }
        usize mask = shard->capacity - 1usize;
        usize probe = (hash as usize) & mask;
        raw Slot*? slot = slot_at(shard->slots, probe);
        while (slot->key > removed_key) {
            probe = (probe + 1usize) & mask;
            slot = slot_at(shard->slots, probe);
        }
        if (slot->key == removed_key) {
            shard->removed -= 1usize;
        }
        *slot = Slot {.key = key, .budget = budget, .size = size};
        shard->used += 1usize;
    }
    unlock(shard);
    core::atomic_fetch_add(&charged, 1usize, core::memory_order::relaxed) as void;
    return true;
}

// Removes the record of a charged allocation; false when it was charged to no budget.
protected bool take(raw void*? allocation, (raw BudgetState*?)* budget, usize* size) {
    if (core::atomic_load(&charged, core::memory_order::relaxed) == 0usize) {
        return false;
    }
    usize key = core::pointer_address(allocation);
    u64 hash = hash_of(key);
    raw Shard* shard = shard_of(hash);
    bool found = false;

    lock(shard);
    unsafe {
        if (shard->capacity != 0usize) {
            usize mask = shard->capacity - 1usize;
            usize probe = (hash as usize) & mask;
            raw Slot*? slot = slot_at(shard->slots, probe);
            while (slot->key != empty_key) {
                if (slot->key == key) {
                    *budget = slot->budget;
                    *size = slot->size;
                    slot->key = removed_key;
                    slot->budget = null;
                    shard->used -= 1usize;
                    shard->removed += 1usize;
                    found = true;
                    break;
                }
                probe = (probe + 1usize) & mask;
                slot = slot_at(shard->slots, probe);
            }
        }
    }
    unlock(shard);
    if (found == true) {
        core::atomic_fetch_sub(&charged, 1usize, core::memory_order::relaxed) as void;
    }
    return found;
}

// ---- The C allocator ----

protected AllocationStatus allocate_aligned(usize size, usize alignment,
                                            raw (raw void*?)* result) {
    unsafe {
        if (alignment <= fundamental_alignment) {
            raw void*? memory = malloc(size as c_size);
            *result = memory;
            if (memory == null) {
                return AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED;
            }
            return AllocationStatus::R_RUNTIME_ALLOCATION_OK;
        }
        if (size > runtime.types::size_maximum - (alignment - 1usize) - word) {
            return AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
        }
        raw void*? base = malloc((size + (alignment - 1usize) + word) as c_size);
        if (base == null) {
            return AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED;
        }
        // The pointer the C allocator gave sits in the word before the aligned storage.
        usize start = core::pointer_address(base) + word;
        usize aligned = (start + (alignment - 1usize)) &
                        (runtime.types::size_maximum ^ (alignment - 1usize));
        raw void*? storage = core::offset_bytes(base, aligned - core::pointer_address(base));
        raw (raw void*?)*? origin =
            core::offset_bytes(storage, 0isize - (word as isize)) as raw (raw void*?)*?;
        *origin = base;
        *result = storage;
    }
    return AllocationStatus::R_RUNTIME_ALLOCATION_OK;
}

// Frees storage of the C allocator without touching the budgets.
protected void release_storage(raw void*? allocation, usize alignment) {
    unsafe {
        if (alignment <= fundamental_alignment) {
            free(allocation);
            return;
        }
        raw (raw void*?)*? origin =
            core::offset_bytes(allocation, 0isize - (word as isize)) as raw (raw void*?)*?;
        free(*origin);
    }
}

protected bool should_fail(raw Allocator*? allocator) {
    unsafe {
        if (allocator == null) {
            return false;
        }
        u64 attempt = core::atomic_fetch_add(&allocator->attempt_count, 1u64 as c_uint64,
                                             core::memory_order::relaxed) as u64 + 1u64;
        u64 fail_at =
            core::atomic_load(&allocator->fail_at_attempt, core::memory_order::relaxed) as u64;
        return (fail_at != 0u64) && (attempt == fail_at);
    }
}

// ---- Exports of r_runtime_budget.h ----

@safety("R-RUNTIME-BUDGET-CREATE", "parent is null or a live budget")
extern "C" raw Budget*? r_runtime_budget_create(raw Budget*? parent, c_bool has_bytes,
                                                c_uint64 bytes, c_bool has_tasks,
                                                c_uint64 tasks) {
    unsafe {
        raw BudgetState*? budget = malloc(sizeof(BudgetState) as c_size) as raw BudgetState*?;
        if (budget == null) {
            return null;
        }
        u64 bytes_limit = unlimited;
        if (as_bool(has_bytes) == true) {
            bytes_limit = bytes as u64;
        }
        u64 tasks_limit = unlimited;
        if (as_bool(has_tasks) == true) {
            tasks_limit = tasks as u64;
        }
        raw BudgetState*? parent_state = state_of_mutable(parent);
        *budget = BudgetState {.bytes_used = 0u64, .tasks_used = 0u64, .bytes_limit = bytes_limit,
                               .tasks_limit = tasks_limit, .parent = parent_state,
                               .references = 1usize, .refusing = false};
        retain(parent_state);
        return handle_of(budget);
    }
}

@safety("R-RUNTIME-BUDGET-REFUSING", "None")
extern "C" raw Budget*? r_runtime_budget_refusing() {
    unsafe {
        return handle_of(&refusing_budget as raw BudgetState*);
    }
}

@safety("R-RUNTIME-BUDGET-RETAIN", "budget is null or a live budget")
extern "C" void r_runtime_budget_retain(raw Budget*? budget) {
    retain(state_of_mutable(budget));
}

@safety("R-RUNTIME-BUDGET-RELEASE", "budget is null or a live budget whose reference is given up")
extern "C" void r_runtime_budget_release(raw Budget*? budget) {
    release(state_of_mutable(budget));
}

@safety("R-RUNTIME-BUDGET-CURRENT", "None")
extern "C" raw Budget*? r_runtime_budget_current() {
    return handle_of(current_budget);
}

@safety("R-RUNTIME-BUDGET-SWAP-CURRENT", "budget is null or a live budget")
extern "C" raw Budget*? r_runtime_budget_swap_current(raw Budget*? budget) {
    raw BudgetState*? previous = current_budget;
    current_budget = state_of_mutable(budget);
    return handle_of(previous);
}

@safety("R-RUNTIME-BUDGET-CHARGE-TASK", "budget is null or a live budget")
extern "C" c_bool r_runtime_budget_charge_task(raw Budget*? budget) {
    raw BudgetState*? state = state_of_mutable(budget);
    if (is_null(state) == true) {
        return as_c_bool(true);
    }
    if (add(state, 1u64, true) == false) {
        refused_by_budget = true;
        return as_c_bool(false);
    }
    return as_c_bool(true);
}

@safety("R-RUNTIME-BUDGET-RETURN-TASK", "budget is null or a live budget charged for the task")
extern "C" void r_runtime_budget_return_task(raw Budget*? budget) {
    raw BudgetState*? state = state_of_mutable(budget);
    if (is_null(state) == false) {
        subtract(state, 1u64, true);
    }
}

@safety("R-RUNTIME-ALLOCATION-REFUSED-BY-BUDGET", "None")
extern "C" c_bool r_runtime_allocation_refused_by_budget() {
    return as_c_bool(refused_by_budget);
}

@safety("R-RUNTIME-BUDGET-NOTE-REFUSAL", "None")
extern "C" void r_runtime_budget_note_refusal() {
    refused_by_budget = true;
}

@safety("R-RUNTIME-BUDGET-BYTES-USED", "budget is null or a live budget")
extern "C" c_uint64 r_runtime_budget_bytes_used(raw const Budget*? budget) {
    raw BudgetState*? state = state_of(budget);
    if (is_null(state) == true) {
        return 0u64 as c_uint64;
    }
    return counter_load(state, false) as c_uint64;
}

@safety("R-RUNTIME-BUDGET-TASKS-USED", "budget is null or a live budget")
extern "C" c_uint64 r_runtime_budget_tasks_used(raw const Budget*? budget) {
    raw BudgetState*? state = state_of(budget);
    if (is_null(state) == true) {
        return 0u64 as c_uint64;
    }
    return counter_load(state, true) as c_uint64;
}

@safety("R-RUNTIME-BUDGET-BYTES-LIMIT", "budget is null or a live budget")
extern "C" c_uint64 r_runtime_budget_bytes_limit(raw const Budget*? budget) {
    raw BudgetState*? state = state_of(budget);
    if (is_null(state) == true) {
        return unlimited as c_uint64;
    }
    return limit_of(state, false) as c_uint64;
}

@safety("R-RUNTIME-BUDGET-TASKS-LIMIT", "budget is null or a live budget")
extern "C" c_uint64 r_runtime_budget_tasks_limit(raw const Budget*? budget) {
    raw BudgetState*? state = state_of(budget);
    if (is_null(state) == true) {
        return unlimited as c_uint64;
    }
    return limit_of(state, true) as c_uint64;
}

@safety("R-RUNTIME-BUDGET-BYTES-AVAILABLE", "budget is null or a live budget")
extern "C" c_uint64 r_runtime_budget_bytes_available(raw const Budget*? budget) {
    u64 available = unlimited;
    raw BudgetState*? cursor = state_of(budget);
    unsafe {
        while (cursor != null) {
            u64 limit = limit_of(cursor, false);
            if (limit != unlimited) {
                u64 used = counter_load(cursor, false);
                u64 room = 0u64;
                if (used < limit) {
                    room = limit - used;
                }
                if (room < available) {
                    available = room;
                }
            }
            cursor = parent_of(cursor);
        }
    }
    return available as c_uint64;
}

// ---- Exports of r_runtime_allocator.h ----

@safety("R-RUNTIME-ALLOCATOR-INITIALIZE", "allocator is writable")
extern "C" void r_runtime_allocator_initialize(raw Allocator* allocator) {
    unsafe {
        core::atomic_store(&allocator->attempt_count, 0u64 as c_uint64,
                           core::memory_order::relaxed);
        core::atomic_store(&allocator->fail_at_attempt, 0u64 as c_uint64,
                           core::memory_order::relaxed);
    }
}

@safety("R-RUNTIME-ALLOCATOR-SET-FAILURE", "allocator is initialized")
extern "C" void r_runtime_allocator_set_failure(raw Allocator* allocator, c_uint64 attempt) {
    unsafe {
        core::atomic_store(&allocator->attempt_count, 0u64 as c_uint64,
                           core::memory_order::relaxed);
        core::atomic_store(&allocator->fail_at_attempt, attempt, core::memory_order::relaxed);
    }
}

@safety("R-RUNTIME-ALLOCATOR-ATTEMPT-COUNT", "allocator is initialized")
extern "C" c_uint64 r_runtime_allocator_attempt_count(raw const Allocator* allocator) {
    unsafe {
        return core::atomic_load(&allocator->attempt_count, core::memory_order::relaxed);
    }
}

@safety("R-RUNTIME-ALLOCATE", "result is writable; the allocator is null or initialized")
extern "C" AllocationStatus r_runtime_allocator_allocate(raw Allocator*? allocator, c_size size,
                                                         c_size alignment,
                                                         raw (raw void*?)* result) {
    unsafe {
        *result = null;
    }
    if ((size as usize) > (runtime.types::maximum_object_size as usize)) {
        return AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    }
    if ((alignment as usize) > (runtime.types::maximum_alignment as usize)) {
        return AllocationStatus::R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT;
    }
    if (size as usize == 0usize) {
        return AllocationStatus::R_RUNTIME_ALLOCATION_OK;
    }
    refused_by_budget = false;
    if (should_fail(allocator) == true) {
        return AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    raw BudgetState*? budget = current_budget;
    if (is_null(budget) == true) {
        return allocate_aligned(size as usize, alignment as usize, result);
    }
    // Core R-STMT-0020: the budget of the executing task and its ancestors pay first.
    if (add(budget, size as u64, false) == false) {
        refused_by_budget = true;
        return AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    AllocationStatus status = allocate_aligned(size as usize, alignment as usize, result);
    if (status != AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
        subtract(budget, size as u64, false);
        return status;
    }
    unsafe {
        if ((is_refusing(budget) == true) || (record(*result, budget, size as usize) == false)) {
            release_storage(*result, alignment as usize);
            subtract(budget, size as u64, false);
            *result = null;
            return AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED;
        }
    }
    retain(budget);
    return AllocationStatus::R_RUNTIME_ALLOCATION_OK;
}

@safety("R-RUNTIME-DEALLOCATE", "the allocation came from the runtime allocator with this alignment, or is null")
extern "C" void r_runtime_allocator_deallocate(raw void*? allocation, c_size alignment) {
    if (is_absent(allocation) == true) {
        return;
    }
    raw BudgetState*? budget = null;
    usize size = 0usize;
    if (take(allocation, &budget, &size) == true) {
        subtract(budget, size as u64, false);
        release(budget);
    }
    release_storage(allocation, alignment as usize);
}

// Puts a charged allocation back in the table after a failed reallocation; without room for its
// record it leaves its budget.
protected void restore(raw void*? allocation, raw BudgetState*? budget, usize charged_size) {
    if (record(allocation, budget, charged_size) == false) {
        subtract(budget, charged_size as u64, false);
        release(budget);
    }
}

@safety("R-RUNTIME-REALLOCATE", "allocation is null or a runtime allocation of old_size bytes at alignment; result is writable")
extern "C" AllocationStatus r_runtime_allocator_reallocate(raw Allocator*? allocator,
                                                           raw void*? allocation,
                                                           c_size old_size, c_size new_size,
                                                           c_size alignment,
                                                           raw (raw void*?)* result) {
    unsafe {
        *result = allocation;
    }
    if ((new_size as usize) > (runtime.types::maximum_object_size as usize)) {
        return AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    }
    if ((alignment as usize) > (runtime.types::maximum_alignment as usize)) {
        return AllocationStatus::R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT;
    }
    if (new_size as usize == 0usize) {
        r_runtime_allocator_deallocate(allocation, alignment);
        unsafe {
            *result = null;
        }
        return AllocationStatus::R_RUNTIME_ALLOCATION_OK;
    }
    if (is_absent(allocation) == true) {
        return r_runtime_allocator_allocate(allocator, new_size, alignment, result);
    }
    refused_by_budget = false;
    if (should_fail(allocator) == true) {
        return AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    usize size = new_size as usize;
    raw BudgetState*? budget = null;
    usize charged_size = 0usize;
    bool recorded = take(allocation, &budget, &charged_size);

    // Core R-STMT-0020: a charged allocation stays with its budget, which pays the growth and
    // receives the shrinkage; an uncharged one stays uncharged.
    if ((recorded == true) && (size > charged_size) &&
        (add(budget, (size - charged_size) as u64, false) == false)) {
        refused_by_budget = true;
        restore(allocation, budget, charged_size);
        return AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    raw void*? replacement = null;
    unsafe {
        if ((alignment as usize) <= fundamental_alignment) {
            replacement = realloc(allocation, new_size);
        } else {
            if (allocate_aligned(size, alignment as usize, &replacement as raw (raw void*?)*) ==
                AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
                usize kept = old_size as usize;
                if (size < kept) {
                    kept = size;
                }
                runtime.types::memcpy(replacement, allocation as raw const void*?, kept as c_size)
                    as void;
                release_storage(allocation, alignment as usize);
            }
        }
    }
    if (is_absent(replacement) == true) {
        if (recorded == true) {
            if (size > charged_size) {
                subtract(budget, (size - charged_size) as u64, false);
            }
            restore(allocation, budget, charged_size);
        }
        return AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED;
    }
    if (recorded == true) {
        if (size < charged_size) {
            subtract(budget, (charged_size - size) as u64, false);
        }
        // Without room for the new record the allocation leaves its budget.
        if (record(replacement, budget, size) == false) {
            subtract(budget, size as u64, false);
            release(budget);
        }
    }
    unsafe {
        *result = replacement;
    }
    return AllocationStatus::R_RUNTIME_ALLOCATION_OK;
}
