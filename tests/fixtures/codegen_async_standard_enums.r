module test.codegen.async_standard_enums;

import std.fs;
import std.process;

/* R-STMT-0006, R-REFL-0001: switches and reflection over fieldless enums of the standard
   library inside async frames, across await. */

async void pause() {}

async i32 classify(std.fs::seek_origin origin) throws std.async::start_error {
    await pause();
    switch (origin) {
    case std.fs::seek_origin::start:
        return 1;
    case std.fs::seek_origin::current:
        return 2;
    case std.fs::seek_origin::end:
        return 3;
    }
}

async usize describe(std.process::termination_kind kind) throws std.async::start_error {
    std.process::termination_kind held = kind;
    await pause();
    constexpr str name = core::enum_name(held);
    return len(name) + core::enum_ordinal(held);
}

async i32 recover(std.alloc::alloc_error failure) throws std.async::start_error {
    await pause();
    switch (failure) {
    case std.alloc::alloc_error::out_of_memory:
        return 5;
    default:
        break;
    }
    o<std.alloc::alloc_error> named =
        core::enum_from_name::<std.alloc::alloc_error>("unsupported_alignment");
    switch (named) {
    case variant o::some(value):
        if (*value == failure) {
            return 7;
        }
        return 8;
    case variant o::none:
        return 9;
    }
}

async i32 main() {
    try {
        if (await classify(std.fs::seek_origin::current) != 2) {
            return 1;
        }
        if (await describe(std.process::termination_kind::other) != 7usize) {
            return 2;
        }
        if (await recover(std.alloc::alloc_error::unsupported_alignment) != 7) {
            return 3;
        }
        if (await recover(std.alloc::alloc_error::out_of_memory) != 5) {
            return 4;
        }
        return 0;
    } catch (std.async::start_error failure) {
        switch (failure) {
        case std.async::start_error::allocation_failed:
            return 90;
        case std.async::start_error::runtime_stopping:
            return 91;
        case std.async::start_error::scope_full:
            return 92;
        case std.async::start_error::budget_exhausted:
            return 93;
        }
    }
}
