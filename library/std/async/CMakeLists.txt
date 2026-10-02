set(R_STD_ASYNC_PUBLIC_SOURCES
    source/acquire.c
    source/add_permits.c
    source/available_permits.c
    source/blocking.c
    source/broadcast.c
    source/broadcast_receive.c
    source/cancel.c
    source/clone_broadcast.c
    source/clone_mutex.c
    source/clone_notify.c
    source/clone_rw_lock.c
    source/clone_semaphore.c
    source/detach.c
    source/lock.c
    source/mutex_guard_mut.c
    source/mutex_guard_ref.c
    source/mutex_new.c
    source/notified.c
    source/notify_all.c
    source/notify_new.c
    source/notify_one.c
    source/publish.c
    source/read.c
    source/release.c
    source/rw_read_guard_ref.c
    source/rw_write_guard_mut.c
    source/rw_write_guard_ref.c
    source/rwlock_new.c
    source/semaphore_new.c
    source/subscribe.c
    source/task_id.c
    source/try_acquire.c
    source/try_lock.c
    source/try_read.c
    source/try_write.c
    source/unlock.c
    source/write.c
)
r_add_library_module(r_std_async std.async std_async ${R_STD_ASYNC_PUBLIC_SOURCES})
target_sources(r_std_async PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/async_sync/source/broadcast.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/async_sync/source/mutex.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/async_sync/source/notify.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/async_sync/source/rwlock.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/async_sync/source/semaphore.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/async_sync/source/waiters.c"
)
target_include_directories(r_std_async PUBLIC "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include")
target_include_directories(r_std_async PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/async_sync/include"
)
target_link_libraries(r_std_async PUBLIC r_std_alloc r_runtime_0_1 Threads::Threads)
