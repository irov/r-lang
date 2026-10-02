set(R_STD_TIME_PUBLIC_SOURCES
    source/as_error.c
    source/duration_add.c
    source/duration_compare.c
    source/duration_from_parts.c
    source/duration_from_seconds.c
    source/duration_multiply.c
    source/duration_nanoseconds.c
    source/duration_seconds.c
    source/duration_sub.c
    source/from_utc.c
    source/instant_add.c
    source/instant_duration.c
    source/monotonic_now.c
    source/sleep_for.c
    source/sleep_until.c
    source/system_add.c
    source/system_now.c
    source/to_utc.c
)
r_add_library_module(r_std_time std.time std_time ${R_STD_TIME_PUBLIC_SOURCES})
target_sources(r_std_time PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/source/clock.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/source/darwin_clock.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/source/duration_arithmetic.c"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/source/sleep.c"
)
target_include_directories(r_std_time PRIVATE
    "${R_LIBRARY_REPOSITORY_ROOT}/library/internal/time/include"
)
target_include_directories(r_std_time PUBLIC
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/error/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/library/std/async/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/runtime/include"
    "${R_LIBRARY_REPOSITORY_ROOT}/runtime/darwin/include"
)
