#ifndef R_COMPILER_STANDARD_SYNC_H
#define R_COMPILER_STANDARD_SYNC_H

/* The payload masks describe the closed public outcomes, including fieldless states. */
#define R_STANDARD_SYNC_OUTCOMES(X)                                                                \
    X(SEND, send_result, sent, disconnected, allocation_failed, 3, 6, NULL)                        \
    X(TRY_SEND, try_send_result, sent, full, disconnected, 3, 6, NULL)                             \
    X(RECV, recv_result, received, disconnected, unused, 2, 1, NULL)                               \
    X(TRY_RECV, try_recv_result, received, empty, disconnected, 3, 1, NULL)                        \
    X(SET, set_result, stored, occupied, unused, 2, 2, NULL)                                       \
    X(LOCK, lock_result, locked, poisoned, would_deadlock, 3, 3, "std.sync::mutex_guard")          \
    X(TRY_LOCK, try_lock_result, locked, poisoned, would_deadlock, 4, 3, "std.sync::mutex_guard")  \
    X(READ, read_lock_result, locked, poisoned, would_deadlock, 3, 3, "std.sync::rw_read_guard")   \
    X(TRY_READ,                                                                                    \
      try_read_lock_result,                                                                        \
      locked,                                                                                      \
      poisoned,                                                                                    \
      would_deadlock,                                                                              \
      4,                                                                                           \
      3,                                                                                           \
      "std.sync::rw_read_guard")                                                                   \
    X(WRITE,                                                                                       \
      write_lock_result,                                                                           \
      locked,                                                                                      \
      poisoned,                                                                                    \
      would_deadlock,                                                                              \
      3,                                                                                           \
      3,                                                                                           \
      "std.sync::rw_write_guard")                                                                  \
    X(TRY_WRITE,                                                                                   \
      try_write_lock_result,                                                                       \
      locked,                                                                                      \
      poisoned,                                                                                    \
      would_deadlock,                                                                              \
      4,                                                                                           \
      3,                                                                                           \
      "std.sync::rw_write_guard")                                                                  \
    X(RESERVE, reserve_result, reserved, disconnected, unused, 2, 1, "std.sync::permit")           \
    X(TRY_RESERVE, try_reserve_result, reserved, full, disconnected, 3, 1, "std.sync::permit")

typedef struct RStandardSyncOutcome {
    const char *name;
    const char *variants[4];
    uint32_t variant_count;
    uint32_t payload_mask;
    const char *guard;
} RStandardSyncOutcome;

static inline const RStandardSyncOutcome *r_standard_sync_outcome_at(size_t index) {
#define R_SYNC_SCHEMA(id, name, first, second, third, count, mask, guard)                          \
    {"std.sync::" #name, {#first, #second, #third, "would_block"}, count, mask, guard},
    static const RStandardSyncOutcome schemas[] = {R_STANDARD_SYNC_OUTCOMES(R_SYNC_SCHEMA)};
#undef R_SYNC_SCHEMA
    return index < sizeof(schemas) / sizeof(schemas[0]) ? &schemas[index] : NULL;
}

#define R_STANDARD_SYNC_GUARDS(X)                                                                  \
    X(mutex_guard, RStdSyncMutexGuard)                                                             \
    X(rw_read_guard, RStdSyncRwReadGuard)                                                          \
    X(rw_write_guard, RStdSyncRwWriteGuard)

#define R_STANDARD_SYNC_LOCK_OPERATIONS(X)                                                         \
    X(MUTEX_NEW, r_std_sync_mutex_new)                                                             \
    X(RWLOCK_NEW, r_std_sync_rwlock_new)                                                           \
    X(LOCK, r_std_sync_lock)                                                                       \
    X(TRY_LOCK, r_std_sync_try_lock)                                                               \
    X(READ, r_std_sync_read)                                                                       \
    X(TRY_READ, r_std_sync_try_read)                                                               \
    X(WRITE, r_std_sync_write)                                                                     \
    X(TRY_WRITE, r_std_sync_try_write)                                                             \
    X(MUTEX_GUARD_REF, r_std_sync_mutex_guard_ref)                                                 \
    X(MUTEX_GUARD_MUT, r_std_sync_mutex_guard_mut)                                                 \
    X(RW_READ_GUARD_REF, r_std_sync_rw_read_guard_ref)                                             \
    X(RW_WRITE_GUARD_REF, r_std_sync_rw_write_guard_ref)                                           \
    X(RW_WRITE_GUARD_MUT, r_std_sync_rw_write_guard_mut)                                           \
    X(UNLOCK, r_std_sync_unlock)                                                                   \
    X(WAIT, r_std_sync_wait)

#endif
