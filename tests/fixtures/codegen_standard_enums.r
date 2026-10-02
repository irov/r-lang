module test.codegen.standard_enums;

import std.fs;
import std.process;
import std.string;
import std.sync;

/* R-STMT-0006, R-REFL-0001: a fieldless enum of the standard library is a fieldless enum in
   every respect: its variants are constants, a switch selects over them, exhaustively or with a
   default, and static reflection names, orders and finds them. */

i32 origin_code(std.fs::seek_origin origin) {
    switch (origin) {
    case std.fs::seek_origin::start:
        return 1;
    case std.fs::seek_origin::current:
        return 2;
    case std.fs::seek_origin::end:
        return 3;
    }
}

i32 alloc_code(std.alloc::alloc_error failure) {
    switch (failure) {
    case std.alloc::alloc_error::out_of_memory:
        return 10;
    case std.alloc::alloc_error::size_overflow:
        return 20;
    default:
        return 30;
    }
}

i32 start_code(std.async::start_error failure) {
    switch (failure) {
    case std.async::start_error::allocation_failed:
        return 1;
    case std.async::start_error::runtime_stopping:
        return 2;
    case std.async::start_error::scope_full:
        return 3;
    case std.async::start_error::budget_exhausted:
        return 4;
    }
}

usize name_width(std.process::termination_kind kind) {
    constexpr str name = core::enum_name(kind);
    return len(name);
}

i32 check_switches() {
    if (origin_code(std.fs::seek_origin::end) != 3) {
        return 1;
    }
    if (alloc_code(std.alloc::alloc_error::size_overflow) != 20) {
        return 2;
    }
    if (alloc_code(std.alloc::alloc_error::unsupported_alignment) != 30) {
        return 3;
    }
    if (start_code(std.async::start_error::scope_full) != 3) {
        return 4;
    }
    std.thread::thread_error thread_failure = std.thread::thread_error::permission_denied;
    if (thread_failure != std.thread::thread_error::permission_denied) {
        return 5;
    }
    std.sync::barrier_error barrier = std.sync::barrier_error::zero_participants;
    std.string::boundary_error boundary = std.string::boundary_error::not_scalar_boundary;
    std.bytes::bytes_error bytes_failure = std.bytes::bytes_error::range_overflow;
    if ((core::enum_ordinal(barrier) != 0usize) || (core::enum_ordinal(boundary) != 1usize) ||
        (core::enum_ordinal(bytes_failure) != 1usize)) {
        return 6;
    }
    return 0;
}

i32 check_reflection() {
    if (name_width(std.process::termination_kind::signalled) != 9usize) {
        return 10;
    }
    constexpr str order = core::enum_name(core::memory_order::acq_rel);
    if (len(order) != 7usize) {
        return 11;
    }
    if (core::enum_count::<std.alloc::alloc_error>() != 4usize) {
        return 12;
    }
    if (core::enum_ordinal(std.fs::seek_origin::current) != 1usize) {
        return 13;
    }
    o<std.process::pipe_mode> piped = core::enum_at::<std.process::pipe_mode>(2usize);
    switch (piped) {
    case variant o::some(mode):
        if (*mode != std.process::pipe_mode::piped) {
            return 14;
        }
        break;
    case variant o::none:
        return 15;
    }
    o<std.process::pipe_mode> beyond = core::enum_at::<std.process::pipe_mode>(3usize);
    switch (beyond) {
    case variant o::some(mode):
        mode as void;
        return 16;
    case variant o::none:
        break;
    }
    o<std.alloc::alloc_error> found =
        core::enum_from_name::<std.alloc::alloc_error>("size_overflow");
    switch (found) {
    case variant o::some(failure):
        if (*failure != std.alloc::alloc_error::size_overflow) {
            return 17;
        }
        break;
    case variant o::none:
        return 18;
    }
    o<std.alloc::alloc_error> missing = core::enum_from_name::<std.alloc::alloc_error>("other");
    switch (missing) {
    case variant o::some(failure):
        failure as void;
        return 19;
    case variant o::none:
        break;
    }
    std.fs::seek_origin[3] origins = core::enum_variants::<std.fs::seek_origin>();
    if ((origins[0] != std.fs::seek_origin::start) || (origins[2] != std.fs::seek_origin::end)) {
        return 20;
    }
    if ((core::enum_min::<std.thread::thread_error>() != std.thread::thread_error::unavailable) ||
        (core::enum_max::<std.thread::thread_error>() !=
         std.thread::thread_error::permission_denied)) {
        return 21;
    }
    return 0;
}

i32 main() {
    const i32 switches = check_switches();
    if (switches != 0) {
        return switches;
    }
    return check_reflection();
}
