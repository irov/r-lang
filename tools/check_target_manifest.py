#!/usr/bin/env python3
"""Validate the first R 0.1 Darwin target manifest and normative rule catalogs."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

from generate_rule_inventory import build_inventory, canonical_json


CORE_REQUIRED_RULES = {
    "R-TYPE-0030",
    "R-TYPE-0012",
    "R-EXPR-0027",
    "R-TERM-0014",
    "R-FUNC-0001",
    "R-ERR-0001",
    "R-ERR-0002",
    "R-ERR-0003",
    "R-CMAP-0039",
    "R-CONF-G012",
}
LIBRARY_REQUIRED_RULES = {
    "R-SLIB-ASYNC-0011",
    "R-SLIB-BITS-0004",
    "R-SLIB-BYTES-0007",
    "R-SLIB-MAP-B009",
    "R-SLIB-IDB-0016",
    "R-SLIB-UTF8-0002",
}
CHECKED_ERROR_CONTRACT = {
    "source_model": "unordered exact nominal throws set",
    "tag_c_type": "uint32_t",
    "success_tag": 0,
    "error_tag_order": "ascending canonical fully-qualified nominal type key",
    "payload_storage": "one union shared by the success value and all checked error payloads",
    "generated_r_to_r_abi": "explicit output-carrier parameter",
    "publication": "tag and payload become observable only after complete initialization",
    "interface_descriptor": "value type, normalized error set, tag table and target layout hash",
    "native_exception_primitives": False,
    "setjmp_longjmp": False,
}
MD5_SHA_1_256_MAXIMUM_OBJECT_SIZE = 2305843009213693951
_C_ABI_INTEGER_ROWS = (
    ("c_char", "char", "char", 1, 8, "char", "signed", "two's-complement", "-128", "127"),
    (
        "c_schar",
        "signed char",
        "signed char",
        1,
        8,
        "char",
        "signed",
        "two's-complement",
        "-128",
        "127",
    ),
    (
        "c_uchar",
        "unsigned char",
        "unsigned char",
        1,
        8,
        "char",
        "unsigned",
        "pure-binary",
        "0",
        "255",
    ),
    ("c_short", "short", "short", 2, 16, "short", "signed", "two's-complement", "-32768", "32767"),
    (
        "c_ushort",
        "unsigned short",
        "unsigned short",
        2,
        16,
        "short",
        "unsigned",
        "pure-binary",
        "0",
        "65535",
    ),
    (
        "c_int",
        "int",
        "int",
        4,
        32,
        "int",
        "signed",
        "two's-complement",
        "-2147483648",
        "2147483647",
    ),
    (
        "c_uint",
        "unsigned int",
        "unsigned int",
        4,
        32,
        "int",
        "unsigned",
        "pure-binary",
        "0",
        "4294967295",
    ),
    (
        "c_long",
        "long",
        "long",
        8,
        64,
        "long",
        "signed",
        "two's-complement",
        "-9223372036854775808",
        "9223372036854775807",
    ),
    (
        "c_ulong",
        "unsigned long",
        "unsigned long",
        8,
        64,
        "long",
        "unsigned",
        "pure-binary",
        "0",
        "18446744073709551615",
    ),
    (
        "c_llong",
        "long long",
        "long long",
        8,
        64,
        "long_long",
        "signed",
        "two's-complement",
        "-9223372036854775808",
        "9223372036854775807",
    ),
    (
        "c_ullong",
        "unsigned long long",
        "unsigned long long",
        8,
        64,
        "long_long",
        "unsigned",
        "pure-binary",
        "0",
        "18446744073709551615",
    ),
    ("c_bool", "_Bool", "_Bool", 1, 8, "bool", "boolean", "c17-_Bool", "0", "1"),
    (
        "c_wchar",
        "wchar_t",
        "int",
        4,
        32,
        "int",
        "signed",
        "two's-complement",
        "-2147483648",
        "2147483647",
    ),
    (
        "c_wint",
        "wint_t",
        "int",
        4,
        32,
        "int",
        "signed",
        "two's-complement",
        "-2147483648",
        "2147483647",
    ),
    (
        "c_int8",
        "int8_t",
        "signed char",
        1,
        8,
        "char",
        "signed",
        "two's-complement",
        "-128",
        "127",
    ),
    (
        "c_uint8",
        "uint8_t",
        "unsigned char",
        1,
        8,
        "char",
        "unsigned",
        "pure-binary",
        "0",
        "255",
    ),
    (
        "c_int16",
        "int16_t",
        "short",
        2,
        16,
        "short",
        "signed",
        "two's-complement",
        "-32768",
        "32767",
    ),
    (
        "c_uint16",
        "uint16_t",
        "unsigned short",
        2,
        16,
        "short",
        "unsigned",
        "pure-binary",
        "0",
        "65535",
    ),
    (
        "c_int32",
        "int32_t",
        "int",
        4,
        32,
        "int",
        "signed",
        "two's-complement",
        "-2147483648",
        "2147483647",
    ),
    (
        "c_uint32",
        "uint32_t",
        "unsigned int",
        4,
        32,
        "int",
        "unsigned",
        "pure-binary",
        "0",
        "4294967295",
    ),
    (
        "c_int64",
        "int64_t",
        "long long",
        8,
        64,
        "long_long",
        "signed",
        "two's-complement",
        "-9223372036854775808",
        "9223372036854775807",
    ),
    (
        "c_uint64",
        "uint64_t",
        "unsigned long long",
        8,
        64,
        "long_long",
        "unsigned",
        "pure-binary",
        "0",
        "18446744073709551615",
    ),
    (
        "c_intptr",
        "intptr_t",
        "long",
        8,
        64,
        "long",
        "signed",
        "two's-complement",
        "-9223372036854775808",
        "9223372036854775807",
    ),
    (
        "c_uintptr",
        "uintptr_t",
        "unsigned long",
        8,
        64,
        "long",
        "unsigned",
        "pure-binary",
        "0",
        "18446744073709551615",
    ),
    (
        "c_intmax",
        "intmax_t",
        "long",
        8,
        64,
        "long",
        "signed",
        "two's-complement",
        "-9223372036854775808",
        "9223372036854775807",
    ),
    (
        "c_uintmax",
        "uintmax_t",
        "unsigned long",
        8,
        64,
        "long",
        "unsigned",
        "pure-binary",
        "0",
        "18446744073709551615",
    ),
    (
        "c_size",
        "size_t",
        "unsigned long",
        8,
        64,
        "long",
        "unsigned",
        "pure-binary",
        "0",
        "18446744073709551615",
    ),
    (
        "c_ptrdiff",
        "ptrdiff_t",
        "long",
        8,
        64,
        "long",
        "signed",
        "two's-complement",
        "-9223372036854775808",
        "9223372036854775807",
    ),
)
_C_ABI_BINARY_FLOAT_ROWS = (
    (
        "c_float",
        "float",
        "float",
        4,
        32,
        "iec-60559-binary32",
        24,
        8,
        -126,
        127,
        -149,
        "0x1p-126",
        "0x1p-149",
        "0x1.fffffep+127",
    ),
    (
        "c_double",
        "double",
        "double",
        8,
        64,
        "iec-60559-binary64",
        53,
        11,
        -1022,
        1023,
        -1074,
        "0x1p-1022",
        "0x1p-1074",
        "0x1.fffffffffffffp+1023",
    ),
    (
        "c_long_double",
        "long double",
        "long double",
        8,
        64,
        "iec-60559-binary64",
        53,
        11,
        -1022,
        1023,
        -1074,
        "0x1p-1022",
        "0x1p-1074",
        "0x1.fffffffffffffp+1023",
    ),
)


def build_expected_c_abi_numeric_types() -> dict[str, Any]:
    types = {
        name: {
            "category": "integer",
            "available": True,
            "c_spelling": c_spelling,
            "canonical_c_type": canonical_c_type,
            "object_size_bytes": object_size_bytes,
            "alignment_bytes": object_size_bytes,
            "width_bits": width_bits,
            "rank": rank,
            "signedness": signedness,
            "representation": representation,
            "minimum": minimum,
            "maximum": maximum,
        }
        for (
            name,
            c_spelling,
            canonical_c_type,
            object_size_bytes,
            width_bits,
            rank,
            signedness,
            representation,
            minimum,
            maximum,
        ) in _C_ABI_INTEGER_ROWS
    }
    for (
        name,
        c_spelling,
        canonical_c_type,
        object_size_bytes,
        width_bits,
        representation,
        significand_bits,
        exponent_bits,
        minimum_normal_exponent,
        maximum_normal_exponent,
        minimum_subnormal_exponent,
        minimum_positive_normal,
        minimum_positive_subnormal,
        maximum_finite,
    ) in _C_ABI_BINARY_FLOAT_ROWS:
        types[name] = {
            "category": "binary_float",
            "available": True,
            "c_spelling": c_spelling,
            "canonical_c_type": canonical_c_type,
            "object_size_bytes": object_size_bytes,
            "alignment_bytes": object_size_bytes,
            "width_bits": width_bits,
            "representation": representation,
            "radix": 2,
            "significand_bits": significand_bits,
            "exponent_bits": exponent_bits,
            "minimum_normal_exponent": minimum_normal_exponent,
            "maximum_normal_exponent": maximum_normal_exponent,
            "minimum_subnormal_exponent": minimum_subnormal_exponent,
            "minimum_positive_normal": minimum_positive_normal,
            "minimum_positive_subnormal": minimum_positive_subnormal,
            "maximum_finite": maximum_finite,
            "has_subnormals": True,
            "has_infinity": True,
            "has_quiet_nan": True,
            "has_signaling_nan": True,
            "has_signed_zero": True,
        }
    return {
        "schema": "r-c-abi-numeric-types-0.1",
        "derived_from": {
            "target": "arm64-apple-macos26.5",
            "compiler": "Apple clang 21.0.0",
            "compiler_build": "clang-2100.3.34.2",
            "sdk": "macOS 27.0",
            "language": "ISO C17",
        },
        "byte_width_bits": 8,
        "integer_rank_order": ["bool", "char", "short", "int", "long", "long_long"],
        "integer_representation_contracts": {
            "two's-complement": {
                "padding_bits": 0,
                "has_trap_representation": False,
            },
            "pure-binary": {
                "padding_bits": 0,
                "has_trap_representation": False,
            },
            "c17-_Bool": {
                "value_bits": 1,
                "padding_bits": 7,
                "has_trap_representation": False,
                "value_encoding": "false=0,true=1",
            },
        },
        "binary_float_contract": {
            "evaluation_method": 0,
            "rounding": "round_to_nearest_ties_to_even",
            "gradual_underflow": True,
            "iec_60559_operation_mapping": {
                "unary_plus": "+",
                "unary_minus": "-",
                "add": "+",
                "subtract": "-",
                "multiply": "*",
                "divide": "/",
            },
        },
        "types": types,
    }


EXPECTED_C_ABI_NUMERIC_TYPES = build_expected_c_abi_numeric_types()
LANE_NATIVE_ENTRIES = {
    "openat",
    "close",
    "fstat",
    "fstatat",
    "lseek",
    "getattrlistbulk",
    "mkdirat",
    "unlinkat",
    "renameatx_np",
    "fsync",
    "fcntl",
}
LANE_EXCLUDED_FAMILIES = {
    "payload_read",
    "payload_write",
    "console",
    "dns",
    "tcp",
    "udp",
    "child_wait",
    "signal_wait",
    "callbacks",
    "r_continuations",
}
CAPABILITY_FAMILIES = {
    "file_payload_read_write",
    "filesystem_metadata_cursor_namespace_durability",
    "console_and_process_pipe_payload",
    "dns",
    "tcp_udp_establishment",
    "tcp_payload",
    "udp_datagram",
    "child_spawn",
    "child_wait",
    "signal_wait",
}
PAYLOAD_OPERATIONS = {
    "random_read",
    "random_write",
    "stream_read",
    "stream_write",
    "shared_borrow_write",
    "ordering_flush",
    "close",
}
PAYLOAD_DESCRIPTOR_CLASSES = {
    "random_access_file",
    "stream_file",
    "special_file",
    "console",
    "process_pipe",
    "stream_socket",
}
# R-SLIB-ASYNC-0019: regular files use the file payload adapter, stream sockets their readiness
# engine; Dispatch I/O remains for other file types, console and process-pipe payload.
PAYLOAD_ENGINES = {
    "dispatch_io": ["special_file", "console", "process_pipe"],
    "file_payload_adapter": ["random_access_file", "stream_file"],
    "nonblocking_socket": ["stream_socket"],
}
PAYLOAD_TERMINAL_ACKNOWLEDGEMENT = (
    "final Dispatch I/O handler or root close cleanup handler for Dispatch I/O handles; for "
    "file and socket handles the request terminal transition after the last native call "
    "returned and root cleanup after the last direct activity"
)
PAYLOAD_BLOCKS_UNITS = {
    "source/io_deadline.c",
    "source/io_dispatch.c",
    "source/io_flush.c",
    "source/io_handle.c",
    "source/io_read.c",
    "source/io_write.c",
}
DNS_RESOLVER_STATUS = "dns-service-get-addr-info-task-bridge-integrated"
DNS_RESOLVER_OPERATIONS = {"std.net::resolve"}
DNS_RESOLVER_START_PRESERVATION = (
    "the complete host byte sequence is copied into immutable task-owned NUL-terminated storage "
    "before task commit; every precommit allocation or runtime-stopping failure performs no DNS "
    "query and leaves the caller borrow unchanged"
)
DNS_RESOLVER_TASK_VALIDATION = (
    "before native submission the task classifies numeric candidates exactly by colon or "
    "decimal-and-dot syntax and otherwise validates ASCII DNS labels, final dot, 63-byte label "
    "and 253-byte total limits, with embedded NUL rejected"
)
DNS_RESOLVER_NUMERIC_RESOLUTION = (
    "parse_ip handles numeric candidates without DNS submission; requested-family mismatch "
    "returns name_not_found with native code zero"
)
DNS_RESOLVER_SUBMISSION = (
    "after task commit and validation DNSServiceGetAddrInfo is submitted without blocking an "
    "executor worker; DNSServiceSetDispatchQueue binds callbacks and deallocation to the request "
    "serial queue"
)
DNS_RESOLVER_RESULT_ORDERING = (
    "accepted v4 and v6 addresses preserve DNSServiceGetAddrInfo callback order, remove "
    "duplicates, retain IPv6 scope and receive the requested host-order port; an empty accepted "
    "set returns name_not_found"
)
DNS_RESOLVER_ERROR_MAPPING = (
    "definitive absence maps to name_not_found, explicitly retryable DNS-SD statuses map to "
    "temporary_failure, narrower portable meanings win, and the exact native status is preserved"
)
DNS_RESOLVER_TERMINAL_ACKNOWLEDGEMENT = (
    "native completion, cancellation and a strict Dispatch deadline use the shared event "
    "sequence; the immutable hostname, address storage, task frame, DNSServiceRef and timer remain "
    "live until DNSServiceRef deallocation and callback/timer quiescence before exactly-once task "
    "acknowledgement"
)
NETWORK_ADAPTER_STATUS = (
    "listen-bind-connect-accept-listener-close-tcp-read-write-shutdown-stream-close-udp-data-"
    "close-task-bridge-integrated-partial"
)
NETWORK_ADAPTER_OPERATIONS = {
    "std.net::tcp_listen",
    "std.net::tcp_accept",
    "std.net::tcp_listener_close",
    "std.net::tcp_connect",
    "std.net::tcp_listener_local_address",
    "std.net::tcp_local_address",
    "std.net::tcp_peer_address",
    "std.net::tcp_read",
    "std.net::tcp_shutdown",
    "std.net::tcp_write",
    "std.net::tcp_write_all",
    "std.net::tcp_close",
    "std.net::udp_bind",
    "std.net::udp_local_address",
    "std.net::udp_send_to",
    "std.net::udp_receive_from",
    "std.net::udp_close",
    "std.net::unix_listen",
    "std.net::unix_accept",
    "std.net::unix_connect",
    "std.net::unix_listener_close",
    "std.net::unix_read_into",
    "std.net::unix_write_from",
    "std.net::unix_write_all_from",
    "std.net::unix_shutdown",
    "std.net::unix_close",
    "std.net::unix_peer_credentials",
    "std.net::unix_datagram_bind",
    "std.net::unix_datagram_connect",
    "std.net::unix_send_from",
    "std.net::unix_receive_into",
    "std.net::unix_datagram_close",
}
NETWORK_UNIX_DOMAIN_SOCKETS = (
    "R-SLIB-NET-0014..0017: Unix-domain listeners, streams and datagram sockets reuse the TCP "
    "listener, TCP stream and UDP socket storages and operation paths; a path converts to "
    "sockaddr_un before task commit, connect uses the same non-blocking connect source, accept "
    "publishes no peer address, a datagram send goes to the connected peer and a receive "
    "reports no sender; listen and datagram bind remove a stale socket file only on request, "
    "and peer credentials come from getpeereid and LOCAL_PEERPID"
)
NETWORK_TCP_WRITE_SUBMISSION = (
    "tcp_write and tcp_write_all reserve the complete request, deadline source and write FIFO "
    "position before task commit; failed reservation preserves the complete named array owner "
    "and performs no payload I/O"
)
NETWORK_TCP_WRITE_COMPLETION = (
    "tcp_write publishes success only after one positive native prefix; tcp_write_all continues "
    "until every byte is accepted or a terminal condition wins; every outcome returns the exact "
    "owner without mutating any byte and preserves positive partial progress"
)
NETWORK_TCP_WRITE_ORDERING = (
    "nonempty writes share one submission-order FIFO independently from reads; an empty buffer "
    "bypasses that FIFO and an expired deadline as a successful side-effect-free no-op"
)
NETWORK_TCP_WRITE_TERMINAL_ACKNOWLEDGEMENT = (
    "native completion, cancellation and strict continuous-clock deadline use the shared event "
    "sequence; task outcome is immutable after first selection, while buffer, request, cached "
    "handle and descriptor remain live through the request terminal transition"
)
NETWORK_TCP_SHUTDOWN_SUBMISSION = (
    "tcp_shutdown reserves the complete native request, optional deadline source, "
    "direction-specific FIFO position and independent stream retain before task commit; failed "
    "reservation performs no half-close, while an already committed direction succeeds without "
    "repeating native shutdown and before an expired deadline"
)
NETWORK_TCP_SHUTDOWN_ORDERING = (
    "read shutdown follows earlier reads, write shutdown follows earlier writes, both waits at "
    "the head of both direction FIFOs, and the opposite direction remains independently "
    "schedulable; later empty payload operations retain their side-effect-free no-op result"
)
NETWORK_TCP_SHUTDOWN_TERMINAL_ACKNOWLEDGEMENT = (
    "successful native shutdown is the idempotent half-close commit and cannot be replaced by a "
    "later cancellation or deadline; storage state is published before later same-direction "
    "work, and the task frame, request, cached handle and descriptor remain live through native "
    "acknowledgement"
)
NETWORK_TCP_CLOSE_SUBMISSION = (
    "tcp_close reserves the cached payload handle, terminal close request and deadline state "
    "before task commit; successful commit consumes the named stream and atomically rejects "
    "later stream operations, while every start failure leaves that owner unchanged"
)
NETWORK_TCP_CLOSE_TERMINAL_CLEANUP = (
    "tcp_close cancels pending read and write requests, waits for their terminal transitions "
    "and the handle root cleanup, closes the original descriptor, detaches the cached handle "
    "and releases the consumed stream exactly once; cancellation, deadline and close failure "
    "never skip these duties, and a pre-existing close failure precedes an expired deadline"
)
NETWORK_TCP_CLOSE_TERMINAL_ACKNOWLEDGEMENT = (
    "close completion, cancellation and strict continuous-clock deadline use one immutable "
    "event-sequence outcome; task acknowledgement occurs only after terminal descriptor and "
    "retained-resource cleanup, including when the task observer has already cancelled or dropped"
)
NETWORK_UDP_DATAGRAM_SUBMISSION = (
    "udp_send_to and udp_receive_from reserve the task, independent socket retain, direction FIFO "
    "node, native request, optional strict timer and receive bounce storage before task commit; "
    "failed start preserves the complete named array owner byte-for-byte and performs no native "
    "datagram operation"
)
NETWORK_UDP_RECEIVE_BUFFER_CONTRACT = (
    "receive reads one datagram into independent bounce storage, commits only the received prefix "
    "into the returned array, leaves its tail unchanged and reports truncation exactly when the "
    "datagram exceeded the supplied buffer; native failure, cancellation and deadline preserve "
    "every byte, while send never mutates the array"
)
NETWORK_UDP_DIRECTION_ORDERING = (
    "sends and receives use separate submission-order FIFOs so each direction is serialized while "
    "the two directions progress independently; zero-length send remains one real native datagram "
    "in the send FIFO"
)
NETWORK_UDP_SEND_TERMINAL_ACKNOWLEDGEMENT = (
    "native acceptance of the complete datagram is the send commit and cannot be replaced by a "
    "later cancellation or deadline; every terminal race uses the shared event sequence and "
    "retains the task, exact buffer owner, socket, queue node and Dispatch sources through native "
    "cancellation acknowledgement"
)
NETWORK_UDP_CLOSE_TERMINAL_CLEANUP = (
    "udp_close consumes the named socket only after complete close reservation and task commit, "
    "rejects later operations, cancels and drains both direction FIFOs, closes the descriptor and "
    "releases the consumed socket exactly once; cancellation, deadline and close failure never "
    "skip cleanup, and a pre-existing close failure precedes an expired deadline"
)
PROCESS_SPAWN_STATUS = "spawn-pipes-wait-terminate-integrated"
PROCESS_SPAWN_OPERATIONS = {
    "std.process::spawn",
    "std.process::take_stderr",
    "std.process::take_stdin",
    "std.process::take_stdout",
    "std.process::terminate",
    "std.process::wait",
}
PROCESS_SPAWN_EXACT_INVOCATION = (
    "posix_spawn with an exact executable path, argv, environment and captured-or-explicit cwd; "
    "no shell and no PATH lookup"
)
PROCESS_SPAWN_START_PRESERVATION = (
    "command remains byte-for-byte ownership-equivalent after every allocator, pipe, Dispatch "
    "I/O root, spawn-attribute and runtime-stopping precommit failure"
)
PROCESS_SPAWN_CREATION_COMMIT = (
    "POSIX_SPAWN_CLOEXEC_DEFAULT plus POSIX_SPAWN_START_SUSPENDED plus POSIX_SPAWN_SETSIGMASK "
    "with an empty mask plus POSIX_SPAWN_SETSIGDEF for every signal whose disposition a "
    "std.signal listener replaced; the process lifecycle source "
    "and registry are installed before the event-sequenced SIGCONT commit"
)
PROCESS_SPAWN_CANCELLATION_DEADLINE = (
    "task cancellation and a strict mach-continuous deadline may select only before creation "
    "commit; duties continue after selection and the first event sequence remains immutable"
)
PROCESS_SPAWN_PIPE_RESERVATION = (
    "all requested parent process-pipe Dispatch I/O STREAM roots are created before task commit "
    "and remain child-owned until extraction or child drop"
)
PROCESS_PIPE_EXTRACTION = (
    "each allocation-free take atomically transfers one parent Dispatch I/O view under the "
    "process registry mutex; exactly one racing caller receives a configured endpoint, every "
    "unavailable state returns o::none and the extracted owner is independent of child "
    "destruction"
)
PROCESS_WAIT_RESERVATION = (
    "wait reserves the task frame, strict deadline source, exclusive observation slot and an "
    "independent native child retain before task commit; every start failure preserves the exact "
    "named child owner"
)
PROCESS_WAIT_COMPLETION = (
    "DispatchSourceProcess performs the sole nonblocking waitpid reap and records one event "
    "sequence; success totalizes the wait status, while a pre-reap deadline returns the same "
    "retryable child owner and all paths wait for timer cancellation acknowledgement"
)
PROCESS_TERMINATE_COMMIT = (
    "terminate independently retains the borrowed child, checks its absolute deadline immediately "
    "before bounded SIGKILL submission and treats native acceptance as a non-cancellable commit; "
    "wait remains the sole status observation operation"
)
PROCESS_CHILD_STATUS_MAPPING = (
    "WIFEXITED maps to exited with WEXITSTATUS, WIFSIGNALED maps to signalled with WTERMSIG, "
    "every other Darwin wait status maps to other with the exact signed 32-bit wait status; "
    "success is true only for exited code zero"
)
PROCESS_SPAWN_ROLLBACK = (
    "every failure after suspended child creation sends SIGKILL, cancels the installed process "
    "source if present and reaps exactly once before task acknowledgement"
)
PROCESS_SPAWN_CHILD_LIFECYCLE = (
    "a runtime-retained child registry and DispatchSourceProcess reap every committed child "
    "exactly once even after the public child view is dropped"
)
PROCESS_SPAWN_HOSTED_SHUTDOWN = (
    "executor drain is followed by child kill/reap, process-source cancellation acknowledgement "
    "and final Dispatch I/O pipe-root cleanup acknowledgement"
)
PROCESS_SPAWN_UNIMPLEMENTED_OPERATIONS: set[str] = set()
PROCESS_SPAWN_CAPABILITY_BACKEND = (
    "serial Dispatch submission of exact-path posix_spawn with START_SUSPENDED"
)
PROCESS_SPAWN_CAPABILITY_ACKNOWLEDGEMENT = (
    "process source installation, SIGCONT creation commit or suspended rollback reap"
)
PUBLIC_STD_IO_INTEGRATED_OPERATIONS = {
    "std.io::read",
    "std.io::write",
    "std.io::write_all",
    "std.io::write_shared",
    "std.io::flush",
    "std.io::stdin",
    "std.io::stdout",
    "std.io::stderr",
    "std.io::close_input",
    "std.io::close_output",
}
PUBLIC_STD_IO_TASK_BRIDGE_OPERATIONS = {
    "std.io::read",
    "std.io::write",
    "std.io::write_all",
    "std.io::write_shared",
    "std.io::flush",
    "std.io::close_input",
    "std.io::close_output",
    "std.fs::read_file",
    "std.fs::read_file_beneath",
}
PUBLIC_STD_IO_UNIMPLEMENTED_OPERATIONS: set[str] = set()
PUBLIC_STD_FS_PAYLOAD_INTEGRATED_OPERATIONS = {
    "std.fs::read",
    "std.fs::write",
    "std.fs::write_all",
    "std.fs::read_at",
    "std.fs::write_all_at",
    "std.fs::read_file",
    "std.fs::read_file_beneath",
}
PUBLIC_STD_FS_INTEGRATED_OPERATIONS = {
    "std.fs::create_directory",
    "std.fs::create_directory_beneath",
    "std.fs::file_metadata",
    "std.fs::metadata",
    "std.fs::metadata_beneath",
    "std.fs::open_directory",
    "std.fs::open_directory_beneath",
    "std.fs::open_file",
    "std.fs::open_file_beneath",
    "std.fs::remove_directory_beneath",
    "std.fs::remove_file_beneath",
    "std.fs::rename_beneath",
    "std.fs::seek",
    "std.fs::flush",
    "std.fs::sync",
    "std.fs::try_lock",
    "std.fs::lock",
    "std.fs::unlock",
    "std.fs::close_file",
    "std.fs::close_directory",
    "std.fs::iterate",
    "std.fs::next",
    "std.fs::read_file",
    "std.fs::read_file_beneath",
}
PAYLOAD_IMPLEMENTATION_STATUS = (
    "std.io-console-close-and-std.fs-payload-task-bridge-integrated-partial"
)
TIMER_CLOCK_BRIDGE = (
    "absolute std.fs payload-operation deadlines retain the mach_continuous_time instant and "
    "recompute the remaining duration on activation and every Dispatch timer wake; std.time "
    "sleep timers currently use one relative Dispatch arm"
)
TIMER_DEADLINE = (
    "strict native one-shot arms; the std.fs payload absolute-deadline adapter re-arms until "
    "the continuous deadline is reached"
)
PAYLOAD_LOW_LEVEL_RELATIVE_DEADLINE = (
    "runtime payload requests receive an adapter-computed relative timeout; std.fs payload "
    "requests pass no native timeout because std_fs_deadline_clock governs their absolute "
    "deadline"
)
STD_FS_SHARED_POSITION_ORDER = (
    "std.fs::read, std.fs::write, std.fs::write_all, std.fs::read_at, std.fs::write_all_at, "
    "std.fs::seek, std.fs::flush and std.fs::sync share one synchronized FIFO in "
    "successful-start submission order"
)
STD_FS_PAYLOAD_HANDLE = (
    "successful std.fs file output materialization creates and owns one payload handle over a "
    "duplicated descriptor before file-result publication, a file payload adapter handle for a "
    "regular file and a Dispatch I/O STREAM handle for any other file type; materialization "
    "failure publishes no file handle, and every payload start only retains that handle"
)
STD_FS_NONAPPEND_POSITIONING = (
    "every read and non-append write of a regular file passes the shared logical absolute "
    "position, or the explicit offset of a positional operation, as the offset of pread or "
    "pwrite on the file payload adapter and never moves the descriptor offset; for any other "
    "file type the position is applied with lseek(SEEK_SET) inside dispatch_io_barrier on its "
    "STREAM handle; a positional operation leaves the shared logical position unchanged"
)
STD_FS_APPEND_POSITIONING = (
    "append writes use write on a regular file and the STREAM handle for any other file type, "
    "on a descriptor retaining O_APPEND, select end atomically for each write and do not change "
    "the shared read/seek observation position"
)
STD_FS_DEADLINE_CLOCK = (
    "std.fs payload operations retain the absolute std.time instant in the mach_continuous_time "
    "domain; activation and every Dispatch timer wake recompute the remaining continuous duration "
    "and re-arm until the absolute deadline is reached"
)
STD_FS_ABSOLUTE_DEADLINE = (
    "for std.fs payload operations the absolute std.time instant is fixed before task commit, is "
    "never converted once into a stored relative timeout and remains effective while waiting in "
    "the shared position FIFO and during native I/O"
)
STD_FS_CANCELLATION_DEADLINE_ACKNOWLEDGEMENT = (
    "queued cancellation or deadline removes the position reservation before acknowledgement; "
    "cancellation before the transfer enters its native call finishes it without that call; "
    "cancellation after entry is acknowledged after the call returns, or for a file that is not "
    "regular after the final Dispatch I/O handler, and the handle, buffer and task frame are "
    "retained until then"
)
STD_FS_EXPLICIT_CLOSE = (
    "std.fs::close_file reserves the handle before task commit, rejects new operations, cancels "
    "and drains registered operations, waits for the handle root cleanup that follows the last "
    "native activity, propagates its native error and releases its view exactly once"
)
STD_FS_WHOLE_FILE_READ_OPERATIONS = {
    "std.fs::read_file",
    "std.fs::read_file_beneath",
}
STD_FS_WHOLE_FILE_READ_PIPELINE = (
    "filesystem-lane open, one per-operation RANDOM payload handle over a duplicated descriptor "
    "(file payload adapter for a regular file, Dispatch I/O otherwise), filesystem-lane close "
    "of the original descriptor, repeated offset-ordered reads and handle root cleanup"
)
STD_FS_WHOLE_FILE_READ_PATH_POLICY = (
    "std.fs::read_file follows an ordinary path final symbolic link; "
    "std.fs::read_file_beneath opens relative to the retained directory with "
    "O_RESOLVE_BENEATH and O_NOFOLLOW_ANY and rejects intermediate and final symbolic links"
)
STD_FS_WHOLE_FILE_READ_LIMIT_EOF = (
    "each read is bounded by the 65536-byte chunk and the remaining limit-plus-one probe; EOF "
    "is authoritative, including a one-byte probe when limit is zero; at most limit bytes "
    "succeeds and an observed byte beyond limit returns file_too_large"
)
STD_FS_WHOLE_FILE_READ_TERMINAL_ACKNOWLEDGEMENT = (
    "publish the task terminal outcome only after filesystem-lane close acknowledgement for the "
    "original descriptor and the root cleanup of the RANDOM payload handle; every error "
    "destroys the partial array before publication"
)
LANE_OPENAT_FLAGS = {
    "O_CLOEXEC",
    "O_NONBLOCK",
    "O_NOFOLLOW",
    "O_RESOLVE_BENEATH",
    "O_NOFOLLOW_ANY",
    "operation-specific access/create flags",
}
LANE_FSTATAT_FLAGS = {
    "AT_SYMLINK_NOFOLLOW",
    "AT_SYMLINK_NOFOLLOW_ANY",
    "AT_RESOLVE_BENEATH",
}
LANE_UNLINKAT_FLAGS = {
    "0",
    "AT_REMOVEDIR",
    "AT_SYMLINK_NOFOLLOW_ANY",
    "AT_RESOLVE_BENEATH",
}
LANE_RENAMEATX_NP_FLAGS = {
    "RENAME_EXCL",
    "RENAME_NOFOLLOW_ANY",
    "RENAME_RESOLVE_BENEATH",
}
FINAL_SYMLINK_ENTRY_OPERATIONS = {
    "std.fs::metadata",
    "std.fs::metadata_beneath",
    "std.fs::remove_file_beneath",
    "std.fs::remove_directory_beneath",
    "std.fs::rename_beneath",
}
DIRECTORY_STAGING_USES = {
    "std.fs::create_directory_beneath",
    "std.fs::write_file_atomic_no_replace",
    "std.fs::write_file_atomic_no_replace_beneath",
}
LANE_BENEATH_RESOLUTION = (
    "kernel-enforced O_RESOLVE_BENEATH/AT_RESOLVE_BENEATH/RENAME_RESOLVE_BENEATH with "
    "corresponding no-follow-any flags; directory creation publishes private root staging with "
    "one renameatx_np; atomic whole-file publication resolves both rename paths from the "
    "independently retained original root"
)
DIRECTORY_STAGING_CLEANUP = (
    "without independent native or foreign mutation: directory creation performs one "
    "root-confined unlinkat before every pre-publication terminal acknowledgement and successful "
    "rename consumes its staging directory; whole-file publication performs exactly one "
    "non-cancellable cleanup after staging creation on failure or success, removing payload if "
    "present and then the staging directory before terminal acknowledgement"
)
ATOMIC_WRITE_BENEATH_PUBLICATION = (
    "one renameatx_np with RENAME_EXCL, RENAME_NOFOLLOW_ANY and RENAME_RESOLVE_BENEATH resolves "
    "both .r-dir-stage-*/payload and destination relative to the independently retained "
    "original root"
)
ATOMIC_WRITE_POSTCOMMIT = (
    "successful rename fixes committed(data); later cancellation, deadline, staging cleanup, "
    "destination-parent F_FULLFSYNC or close cannot select failed; acknowledgement waits for "
    "cleanup and resource release"
)
ATOMIC_NO_REPLACE_CONTRACT = {
    "staging_directory": (
        "ordinary: private destination sibling; beneath: private original-root child; named "
        ".r-dir-stage-* with mode 0700 subject to process umask"
    ),
    "payload": "one regular file named payload with mode 0600 subject to process umask",
    "payload_io": (
        "complete offset-ordered Dispatch I/O write through a per-operation DISPATCH_IO_RANDOM root"
    ),
    "prepublication_durability": "successful payload F_FULLFSYNC before publication",
    "publication": (
        "one renameatx_np with RENAME_EXCL; beneath source and destination resolve from the "
        "independently retained original root"
    ),
    "cleanup": (
        "after staging creation exactly one non-cancellable cleanup removes payload if present "
        "and then the private directory after failure or success"
    ),
    "postcommit": (
        "destination-parent F_FULLFSYNC and all cleanup complete before acknowledgement; no later "
        "outcome changes committed(data)"
    ),
}
DARWIN_TOOLCHAIN_CONTRACT = {
    "c_compiler": "Apple clang",
    "c_compiler_version": "21.0.0",
    "c_compiler_build": "clang-2100.3.34.2",
    "sdk": "macOS 27.0",
    "generated_application_language": "ISO C17",
    "generated_application_extensions": False,
    "darwin_adapter_language": "Clang C17 with Blocks",
    "warnings_as_errors": True,
}
DARWIN_STACK_CONTRACT = {
    "architecture": "arm64",
    "growth_direction": "down",
    "bounds": {
        "thread_api": "pthread_self",
        "address_api": "pthread_get_stackaddr_np",
        "size_api": "pthread_get_stacksize_np",
        "interpretation": (
            "high=stackaddr; low=high-size with overflow-checked uintptr_t arithmetic"
        ),
        "storage": "_Thread_local",
        "allocation": False,
        "stdio": False,
        "locking": False,
    },
    "protected_low_bytes": 65536,
    "call_transition_bytes": 16384,
    "generated_frame_ceiling_bytes": 262144,
    "frame_measurement": {
        "compiler": "Apple clang 21.0.0",
        "compile_flags": [
            "-std=c17",
            "-O0",
            "-fstack-usage",
            "-fno-inline-functions",
            "-fno-lto",
        ],
        "accepted_frame_kind": "static",
        "generated_source_only": True,
        "measurement_define": "R_STACK_USAGE_MEASUREMENT=1",
        "runtime_header": (
            "deterministic R_STACK_FRAME_<function> macros from Clang .su records"
        ),
        "artifact_pipeline": {
            "bootstrap_object": {
                "role": "seed-bounds-only",
                "compile_mode": "R_STACK_USAGE_MEASUREMENT=1",
                "includes_bounds_header": False,
                "linked": False,
            },
            "bounds_header": {
                "format": "R_STACK_FRAME_<function>",
                "ordering": "function-name-ascending",
                "function_selection": "canonical-generated-source-path-equality",
                "frame_kind": "static",
                "requires_nonempty_function_set": True,
                "initialization": "bootstrap-frame-size",
                "update": "per-function-max-previous-bound-and-candidate-frame",
            },
            "fixed_point": {
                "candidate_includes_bounds_header": True,
                "header_injection_flag": "-include",
                "candidate_remeasured": True,
                "function_name_set": "exactly-equal-to-bootstrap-on-every-iteration",
                "stability": "updated-bounds-exactly-equal-to-previous-bounds",
                "maximum_candidate_iterations": 16,
                "cap_exhaustion": "hard-fail-without-link",
            },
            "final_object": {
                "source": "converged-candidate-object",
                "includes_stable_bounds_header": True,
                "remeasured": True,
                "function_name_set": "exactly-equal-to-bootstrap",
                "per_function_bound": "final-less-than-or-equal-to-stable-bound",
                "bounds_header_identity": "exact-header-used-for-final-object",
                "link_input_identity": "exact-converged-candidate-object",
            },
        },
        "instrumented_builds": {
            "configurations": ["address-undefined", "thread"],
            "stack_conformance": False,
            "stack_usage_report": "diagnostic-only",
            "dynamic_or_unknown_frames": "not-validated",
            "nonconforming_marker_suffix": ".stack-usage.nonconforming",
            "link_input_identity": "instrumented-final-object",
        },
        "link_time_optimization": False,
        "generated_function_inlining": False,
    },
    "entry_budget_bytes": 393216,
    "preflight": {
        "api": "r_runtime_stack_require",
        "failure": "stack_exhaustion panic at the entry into R code",
        "policy": "static-entry-bound",
        "call_graph": "acyclic R call graph; owned storage may self-nest through own/rc/arc, o<...>, fixed arrays and array/list/dict values, destroyed iteratively by r_runtime_drop_iterative (Core R-FUNC-0004)",
        "sync_callee_gate": "none",
        "type_glue_move_drop": "none",
        "entry_gates": [
            "hosted-entry-before-main-body",
            "spawned-thread-entry-before-body",
            "async-step-gate",
            "async-initialize-drop-start-launch-gates",
            "call-once-initializer-wrapper",
            "generated-callback-wrapper-before-body",
        ],
        "entry_bound_header": (
            "R_STACK_ENTRY_<entry> = frame(entry) + longest callee path from Clang .su "
            "records, a recursive set C counting depth(m) * frame(m) for each member m plus "
            "max(largest frame of C, deepest callee outside C); tools/compute_stack_entries.py"
        ),
        "indirect_call_marker": (
            "/* R_STACK_INDIRECT: <callback functions> */ before raw fn calls"
        ),
        "recursion_marker": (
            "/* R_STACK_RECURSION: <depth> */ in each function with @recursion (R-FUNC-0026)"
        ),
    },
    "thread_initialization": {
        "api": "r_runtime_stack_initialize_current_thread",
        "hosted_entry": "before source entry; failure panics stack_exhaustion",
        "attached_thread": "before attachment commit; failure returns resource_exhausted",
        "executor_worker": "before task body execution; failure panics stack_exhaustion",
    },
}


FREESTANDING_PROFILE = "freestanding"
HOSTED_MANIFEST_PATH = Path("targets/arm64-apple-darwin.hosted-native-async.json")

# L39 (Core R-ERR-0005, R-IDB-005): the hosted target unwinds by explicit propagation.
HOSTED_PANIC_CONTRACT = {
    "strategy": "unwind",
    "propagation": (
        "a per-thread panic state tested after calls and panic sites; no native exception "
        "unwinding and no setjmp/longjmp"
    ),
    "runtime_panics": "a panic inside runtime or library C code aborts (R-ERR-0006)",
    "unobserved_reports": (
        "delivered once to the panic hook, which writes the report line to the diagnostic sink"
    ),
    "diagnostic_sink": "file descriptor 2 through allocation-free write",
    "stack_exhaustion": "allocation-free panic followed by abort",
}

FREESTANDING_PANIC_CONTRACT = {
    "strategy": "environment-handler",
    "handler": "r_runtime_environment_panic",
    "diagnostic_sink": "environment handler; the runtime writes nothing",
    "stack_exhaustion": "environment handler with category stack_exhaustion",
}
FREESTANDING_STACK_CONTRACT = {
    **DARWIN_STACK_CONTRACT,
    "bounds": {
        "source": "environment",
        "adopt_api": "r_runtime_freestanding_stack_adopt",
        "release_api": "r_runtime_freestanding_stack_release",
        "interpretation": (
            "low and high supplied by the environment; protected low band added with "
            "overflow-checked uintptr_t arithmetic"
        ),
        "storage": "_Thread_local",
        "allocation": False,
        "stdio": False,
        "locking": False,
    },
    "thread_initialization": (
        "the environment adopts the bounds of each thread before its first entry into R code"
    ),
}
FREESTANDING_ALLOCATOR_CONTRACT = {
    "available": False,
    "diagnostic": "R-DIAG-PROFILE-001",
    "rejected_forms": [
        "new",
        "core::adopt",
        "own",
        "arc",
        "rc",
        "weak",
        "array",
        "list",
        "dict",
        "bytes",
        "std.*",
    ],
}
FREESTANDING_THREADS_CONTRACT = {
    "implementation": "environment",
    "profile": "freestanding",
    "scheduling_guarantee": (
        "no R thread creation; the environment adopts stack bounds per thread"
    ),
}
FREESTANDING_FLOATING_CONTRACT = {
    "mode": "environment-managed",
    "rounding": "FE_TONEAREST expected at every entry",
    "exception_flags": "not saved or restored by generated code",
    "extra_controls": "environment responsibility",
}
FREESTANDING_CONTRACT = {
    "runtime_library": "r_runtime_freestanding",
    "runtime_sources": [
        "runtime/freestanding/source/panic.c",
        "runtime/freestanding/source/stack.c",
        "runtime/freestanding/source/thread_local.c",
    ],
    "include_order": ["runtime/freestanding/include", "runtime/include"],
    "generated_includes": [
        "r_runtime_core.h",
        "r_runtime_freestanding.h",
        "r_runtime_target_abi.h",
    ],
    "program_entry": "r_freestanding_main",
    "environment_supplies": ["r_runtime_environment_panic"],
    "environment_calls": [
        "r_runtime_freestanding_stack_adopt",
        "r_runtime_freestanding_stack_release",
        "r_runtime_freestanding_thread_exit",
        "r_freestanding_main",
    ],
    "c_library_symbols": ["memcpy", "memset"],
    "loader_symbols": ["__tlv_bootstrap"],
    "thread_local_support": (
        "Mach-O thread-local variables; the environment loader resolves __tlv_bootstrap"
    ),
    "hosted_entry": False,
    "c_entry_guards": False,
}
# Sections a freestanding manifest shares byte-for-byte with the hosted manifest of the same
# target triple: the C ABI does not depend on the selected profile.
FREESTANDING_SHARED_CORE_FIELDS = (
    "data_model",
    "byte_order",
    "usize_width",
    "isize_width",
    "usize_c_type",
    "isize_c_type",
    "c_abi_numeric_types",
    "aggregate_layout",
    "calling_convention",
    "checked_errors",
    "module_path",
    "atomics",
    "pointer_integer_exposure",
    "lone_cr_source_warning",
)


class ValidationErrors:
    def __init__(self) -> None:
        self.messages: list[str] = []

    def require(self, condition: bool, message: str) -> None:
        if not condition:
            self.messages.append(message)


def load_object(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path}: root must be an object")
    return value


def string_set(values: Any) -> set[str] | None:
    if not isinstance(values, list) or not all(isinstance(value, str) for value in values):
        return None
    return set(values)


def validate_c_abi_numeric_types(core: dict[str, Any], errors: ValidationErrors) -> None:
    actual = core.get("c_abi_numeric_types")
    expected = EXPECTED_C_ABI_NUMERIC_TYPES
    errors.require(isinstance(actual, dict), "C ABI numeric type table must be an object")
    if not isinstance(actual, dict):
        return

    errors.require(
        set(actual) == set(expected),
        "C ABI numeric type table field set is not closed",
    )
    for field, expected_value in expected.items():
        if field == "types":
            continue
        errors.require(
            actual.get(field) == expected_value,
            f"C ABI numeric type table field {field} does not match the pinned target",
        )

    actual_types = actual.get("types")
    expected_types = expected["types"]
    errors.require(isinstance(actual_types, dict), "C ABI numeric types must be an object")
    if not isinstance(actual_types, dict) or not isinstance(expected_types, dict):
        return
    errors.require(
        set(actual_types) == set(expected_types),
        "C ABI numeric type set is not closed",
    )
    for name, expected_record in expected_types.items():
        actual_record = actual_types.get(name)
        errors.require(
            isinstance(actual_record, dict),
            f"C ABI numeric type {name} must be an object",
        )
        if not isinstance(actual_record, dict) or not isinstance(expected_record, dict):
            continue
        errors.require(
            set(actual_record) == set(expected_record),
            f"C ABI numeric type {name} field set is not closed",
        )
        for field, expected_value in expected_record.items():
            errors.require(
                actual_record.get(field) == expected_value,
                f"C ABI numeric type {name} field {field} does not match the pinned target",
            )


def validate_catalog(
    specification_path: Path,
    inventory_path: Path,
    required_rules: set[str],
    expected_count: int,
    errors: ValidationErrors,
) -> dict[str, Any]:
    inventory = load_object(inventory_path)
    expected = build_inventory(specification_path)
    errors.require(
        canonical_json(inventory) == canonical_json(expected),
        f"stale normative rule inventory: {inventory_path}",
    )
    errors.require(
        inventory.get("catalog_kind") == "normative_rule_catalog",
        f"{inventory_path}: wrong catalog_kind",
    )
    errors.require(
        inventory.get("implementation_coverage") is None,
        f"{inventory_path}: normative catalog must not claim implementation coverage",
    )
    errors.require(
        inventory.get("rule_count") == expected_count,
        f"{inventory_path}: expected {expected_count} normative rules",
    )
    records = inventory.get("rules")
    ids = {
        record.get("id")
        for record in records
        if isinstance(records, list) and isinstance(record, dict)
    } if isinstance(records, list) else set()
    errors.require(
        required_rules <= ids,
        f"{inventory_path}: missing required rules {sorted(required_rules - ids)}",
    )
    return inventory


def validate_lane(manifest: dict[str, Any], errors: ValidationErrors) -> None:
    async_record = manifest.get("hosted_native_async")
    errors.require(isinstance(async_record, dict), "hosted_native_async must be an object")
    if not isinstance(async_record, dict):
        return
    executor = async_record.get("executor")
    errors.require(isinstance(executor, dict), "executor must be an object")
    if isinstance(executor, dict):
        errors.require(
            executor.get("executor_workers_may_block") is False,
            "executor workers must never block",
        )
        executor_implementation = executor.get("implementation")
        errors.require(
            isinstance(executor_implementation, dict),
            "executor implementation must be an object",
        )
        if isinstance(executor_implementation, dict):
            errors.require(
                executor_implementation.get("status")
                == "type-erased-task-runtime-hosted-integrated-partial",
                "executor implementation status must remain explicitly partial",
            )
            errors.require(
                executor_implementation.get("cmake_target") == "r_runtime_darwin_task",
                "executor implementation must name its CMake target",
            )
            for field in (
                "start_failure_operand_preservation",
                "completion_release_acquire",
                "cancel_detach_drop",
            ):
                errors.require(
                    executor_implementation.get(field) == "implemented",
                    f"executor implementation must record {field} as implemented",
                )
            for field in ("stackless_continuation_await", "profile_conformance_claim"):
                errors.require(
                    executor_implementation.get(field) is False,
                    f"partial executor implementation must keep {field} false",
                )
            errors.require(
                executor_implementation.get("native_io_task_bridge") is True,
                "executor must record the implemented native I/O task bridge",
            )
            errors.require(
                string_set(executor_implementation.get("native_io_task_bridge_scope"))
                == PUBLIC_STD_IO_TASK_BRIDGE_OPERATIONS,
                "executor native I/O task bridge scope is not closed",
            )
            errors.require(
                executor_implementation.get("hosted_entry_integration") is True,
                "executor must record hosted lifecycle integration",
            )
            resumable = executor_implementation.get("resumable_computation_primitive")
            errors.require(
                isinstance(resumable, dict),
                "executor must record its resumable computation primitive",
            )
            if isinstance(resumable, dict):
                expected_resumable = {
                    "status": "implemented",
                    "prepare_api": "r_runtime_task_resumable_start_prepare",
                    "await_api": "r_runtime_task_execution_await",
                    "suspension_allocation": False,
                    "wait_registration": "single inline waiter with register-and-recheck",
                    "completion_wakeup": (
                        "release publication followed by at-most-once continuation dispatch"
                    ),
                    "cancellation_wakeup": (
                        "a suspended frame is requeued for compiler-generated cleanup"
                    ),
                    "retention": (
                        "frame, awaited observation and result storage remain live until "
                        "terminal acknowledgement"
                    ),
                }
                errors.require(
                    resumable == expected_resumable,
                    "executor resumable computation primitive does not match the runtime ABI",
                )

    timers = async_record.get("timers")
    errors.require(isinstance(timers, dict), "timers must be an object")
    if isinstance(timers, dict):
        errors.require(
            timers.get("clock_bridge") == TIMER_CLOCK_BRIDGE,
            "timer clock bridge must distinguish std.fs continuous re-arming from std.time sleep",
        )
        errors.require(
            timers.get("deadline") == TIMER_DEADLINE,
            "timer deadline contract must record std.fs continuous re-arming",
        )

    lane = async_record.get("filesystem_adapter_lane")
    errors.require(isinstance(lane, dict), "filesystem_adapter_lane must be an object")
    if not isinstance(lane, dict):
        return
    errors.require(lane.get("enabled") is True, "filesystem adapter lane must be enabled")
    errors.require(lane.get("thread_count") == 4, "filesystem adapter lane must have 4 threads")
    errors.require(
        lane.get("disjoint_from_executor_workers") is True,
        "filesystem adapter lane must be disjoint from executor workers",
    )
    errors.require(
        lane.get("may_execute_r_code") is False,
        "filesystem adapter lane must not execute R code",
    )
    maximum_pending = lane.get("maximum_pending_requests")
    errors.require(
        maximum_pending == 65536,
        "filesystem adapter lane queue bound must be exactly 65536",
    )
    errors.require(
        lane.get("queued_cancellation") == "remove before native entry",
        "queued lane cancellation must remove work before native entry",
    )
    errors.require(
        lane.get("entered_cancellation")
        == "record event and acknowledge only after native return",
        "entered lane cancellation must wait for native return",
    )
    errors.require(
        lane.get("entered_deadline") == "record event and acknowledge only after native return",
        "entered lane deadline must wait for native return",
    )

    entries = lane.get("allowed_native_entries")
    errors.require(isinstance(entries, list), "allowed_native_entries must be an array")
    by_name: dict[str, dict[str, Any]] = {}
    if isinstance(entries, list):
        for entry in entries:
            if not isinstance(entry, dict) or not isinstance(entry.get("name"), str):
                errors.messages.append("every allowed native entry must have a string name")
                continue
            name = entry["name"]
            if name in by_name:
                errors.messages.append(f"duplicate lane native entry: {name}")
                continue
            by_name[name] = entry
            errors.require(
                isinstance(entry.get("commit"), str) and bool(entry["commit"].strip()),
                f"lane native entry {name} must document its commit point",
            )
    errors.require(
        set(by_name) == LANE_NATIVE_ENTRIES,
        "filesystem adapter lane native-entry set is not closed",
    )
    openat = by_name.get("openat")
    if openat is not None:
        errors.require(
            string_set(openat.get("flags")) == LANE_OPENAT_FLAGS,
            "lane native entry openat flag set is not closed",
        )
        errors.require(
            openat.get("beneath_resolution")
            == "one atomic openat relative to the retained root with O_RESOLVE_BENEATH and O_NOFOLLOW_ANY",
            "lane native entry openat must record atomic beneath resolution",
        )
        errors.require(
            openat.get("open_or_create")
            == "at most 16 O_CREAT|O_EXCL create probes, with an existing-file open between probes; exhaustion returns EAGAIN",
            "lane native entry openat must record the bounded open_or_create sequence",
        )
    for name, exact_flags in (
        ("fstatat", LANE_FSTATAT_FLAGS),
        ("unlinkat", LANE_UNLINKAT_FLAGS),
        ("renameatx_np", LANE_RENAMEATX_NP_FLAGS),
    ):
        entry = by_name.get(name)
        if entry is None:
            continue
        errors.require(
            string_set(entry.get("flags")) == exact_flags,
            f"lane native entry {name} flag set is not closed",
        )
    fcntl = by_name.get("fcntl")
    if fcntl is not None:
        errors.require(
            string_set(fcntl.get("flags")) == {"F_FULLFSYNC", "F_BARRIERFSYNC", "F_OFD_SETLK"},
            "lane native entry fcntl flag set is not closed",
        )
    excluded = string_set(lane.get("excluded_families"))
    errors.require(
        excluded == LANE_EXCLUDED_FAMILIES,
        "filesystem adapter lane excluded-family set is not closed",
    )

    implementation = lane.get("implementation")
    errors.require(isinstance(implementation, dict), "lane implementation must be an object")
    if isinstance(implementation, dict):
        errors.require(
            implementation.get("status") == "typed-adapter-task-bridge-integrated-partial",
            "lane implementation status must remain explicitly partial",
        )
        errors.require(
            implementation.get("cmake_target") == "r_runtime_darwin_fs_lane",
            "lane implementation must name its CMake target",
        )
        errors.require(
            implementation.get("source_directory") == "runtime/darwin",
            "lane implementation must name its source directory",
        )
        errors.require(
            implementation.get("worker_backend") == "pthread"
            and implementation.get("worker_count_runtime_constant") == 4,
            "lane implementation must use exactly four pthread workers",
        )
        supported_entries = string_set(implementation.get("supported_native_entries"))
        errors.require(
            supported_entries == LANE_NATIVE_ENTRIES,
            "lane implemented native-entry set must exactly match the manifest whitelist",
        )
        errors.require(
            implementation.get("lifecycle_states")
            == ["prepared", "queued", "entered", "terminal"],
            "lane implementation lifecycle must be prepared/queued/entered/terminal",
        )
        errors.require(
            implementation.get("native_reservation_before_task_commit") is True,
            "lane task bridge must reserve native state before task commit",
        )
        errors.require(
            implementation.get("start_failure_operand_preservation")
            == "implemented and failure-injected",
            "lane task bridge must record failure-injected operand preservation",
        )
        errors.require(
            implementation.get("borrowed_submission_resources")
            == "path copied and root descriptor duplicated before task start",
            "lane task bridge must record its borrowed-resource retention",
        )
        errors.require(
            implementation.get("beneath_resolution") == LANE_BENEATH_RESOLUTION,
            "lane implementation must record atomic beneath resolution",
        )
        errors.require(
            implementation.get("maximum_path_bytes") == 1048575,
            "lane implementation maximum path must be exactly 1048575 bytes",
        )
        errors.require(
            implementation.get("open_or_create_retry_limit") == 16,
            "lane implementation open_or_create retry limit must be exactly 16",
        )
        errors.require(
            implementation.get("open_or_create_retry_exhaustion")
            == "EAGAIN mapped to resource_exhausted",
            "lane implementation must record open_or_create retry exhaustion",
        )
        errors.require(
            implementation.get("directory_staging_prefix") == ".r-dir-stage-",
            "lane directory staging prefix must be exactly .r-dir-stage-",
        )
        errors.require(
            implementation.get("directory_staging_mode") == "0700 subject to process umask",
            "lane directory staging mode must be exactly 0700 subject to process umask",
        )
        errors.require(
            string_set(implementation.get("directory_staging_uses")) == DIRECTORY_STAGING_USES,
            "lane directory staging use set is not closed",
        )
        errors.require(
            implementation.get("directory_staging_payload_name") == "payload",
            "lane file staging payload name must be exactly payload",
        )
        errors.require(
            implementation.get("directory_staging_payload_mode")
            == "0600 subject to process umask",
            "lane file staging payload mode must be exactly 0600 subject to process umask",
        )
        errors.require(
            implementation.get("directory_staging_collision_retry_limit") == 16,
            "lane directory staging collision retry limit must be exactly 16",
        )
        errors.require(
            implementation.get("directory_staging_collision_exhaustion")
            == "EAGAIN mapped to resource_exhausted",
            "lane implementation must record directory staging retry exhaustion",
        )
        errors.require(
            implementation.get("directory_staging_namespace_owner") == "R runtime exclusively",
            "lane implementation must reserve the directory staging namespace for the R runtime",
        )
        errors.require(
            implementation.get("directory_staging_safe_api_access")
            == "safe std.fs filesystem operations reject every path operand selecting a reserved staging component before native submission",
            "lane implementation must record safe API staging namespace isolation",
        )
        errors.require(
            implementation.get("directory_staging_foreign_mutation_boundary")
            == "independent native or foreign mutation is outside staging identity and exactly-once cleanup guarantees; capability confinement remains mandatory",
            "lane implementation must record the foreign staging mutation boundary",
        )
        errors.require(
            implementation.get("directory_staging_cleanup") == DIRECTORY_STAGING_CLEANUP,
            "lane implementation must record exact directory staging cleanup",
        )
        errors.require(
            implementation.get("directory_staging_cleanup_failure")
            == "allocation-free abort before terminal acknowledgement because exact cleanup is a runtime invariant",
            "lane implementation must record directory staging cleanup failure handling",
        )
        errors.require(
            implementation.get("atomic_write_beneath_publication")
            == ATOMIC_WRITE_BENEATH_PUBLICATION,
            "lane implementation must record root-relative beneath atomic-write publication",
        )
        errors.require(
            implementation.get("atomic_write_postcommit") == ATOMIC_WRITE_POSTCOMMIT,
            "lane implementation must record atomic-write post-commit outcome stability",
        )
        errors.require(
            string_set(implementation.get("final_symlink_entry_operations"))
            == FINAL_SYMLINK_ENTRY_OPERATIONS,
            "lane final-symlink entry operation set is not closed",
        )
        errors.require(
            implementation.get("runtime_retained_handle_lifecycle") == "implemented",
            "lane task bridge must record the real filesystem handle lifecycle",
        )
        errors.require(
            implementation.get("directory_iteration_cursor")
            == "openat(root, \".\", O_DIRECTORY | O_CLOEXEC) creates an independent open-file description",
            "lane implementation must record its independent directory iteration cursor",
        )
        errors.require(
            implementation.get("handle_close_drain")
            == "successful close start cancels and acknowledges every earlier registered handle operation before close acknowledgement",
            "lane implementation must record handle-close draining",
        )
        errors.require(
            implementation.get("close_deadline_precedence")
            == "pre-existing descriptor failure probed before deadline wins; otherwise expired deadline wins after descriptor release",
            "lane implementation must record close deadline precedence",
        )
        errors.require(
            implementation.get("filesystem_error_closed_native_mapping")
            == "EBADF for a retained filesystem identity maps to std.fs::closed (19)",
            "lane implementation must record the closed filesystem error mapping",
        )
        errors.require(
            implementation.get("committed_completion_override")
            == "an earlier native terminal event or successful create, truncate, final-directory, removal or rename commit replaces an unacknowledged task cancellation selection",
            "lane task bridge must record committed completion override semantics",
        )
        for field in (
            "cancellation_deadline_acknowledgement",
            "retained_submission_resources",
            "enumeration_retry_cache",
            "allocator_integration",
        ):
            errors.require(
                implementation.get(field) == "implemented",
                f"lane implementation must record {field} as implemented",
            )
        errors.require(
            implementation.get("trace_provider_status") == "not_implemented",
            "unavailable lane tracing must remain explicit",
        )
        errors.require(
            implementation.get("task_runtime_integration") is True,
            "lane must record task-runtime integration",
        )
        errors.require(
            implementation.get("public_std_fs_integration") is True,
            "lane must record public std.fs integration",
        )
        errors.require(
            string_set(implementation.get("public_std_fs_integrated_operations"))
            == PUBLIC_STD_FS_INTEGRATED_OPERATIONS,
            "lane integrated std.fs operation set is not closed",
        )
        errors.require(
            implementation.get("profile_conformance_claim") is False,
            "partial lane implementation must not claim profile conformance",
        )

    payload = async_record.get("dispatch_io_payload_adapter")
    errors.require(isinstance(payload, dict), "dispatch_io_payload_adapter must be an object")
    payload_implementation = payload.get("implementation") if isinstance(payload, dict) else None
    errors.require(
        isinstance(payload_implementation, dict),
        "payload adapter implementation must be an object",
    )
    if isinstance(payload_implementation, dict):
        errors.require(
            payload_implementation.get("status") == PAYLOAD_IMPLEMENTATION_STATUS,
            "payload adapter status must identify its partial std.io and std.fs integration",
        )
        errors.require(
            payload_implementation.get("cmake_target") == "r_runtime_darwin_io",
            "payload adapter must name its CMake target",
        )
        errors.require(
            payload_implementation.get("source_directory") == "runtime/darwin",
            "payload adapter must name its source directory",
        )
        errors.require(
            payload_implementation.get("system_dependency") == "Darwin libdispatch",
            "payload adapter must use system libdispatch",
        )
        operations = string_set(payload_implementation.get("supported_operations"))
        errors.require(
            operations == PAYLOAD_OPERATIONS,
            "payload adapter operation set is not closed",
        )
        descriptor_classes = string_set(payload_implementation.get("descriptor_classes"))
        errors.require(
            descriptor_classes == PAYLOAD_DESCRIPTOR_CLASSES,
            "payload adapter descriptor-class set is not closed",
        )
        blocks_units = string_set(payload_implementation.get("blocks_enabled_units"))
        errors.require(
            blocks_units == PAYLOAD_BLOCKS_UNITS,
            "payload adapter Blocks-unit set is not closed",
        )
        for field in (
            "allocator_integration",
            "owned_buffer_two_phase_start",
            "shared_owner_two_phase_start",
            "partial_progress",
            "eof",
        ):
            errors.require(
                payload_implementation.get(field) == "implemented",
                f"payload adapter must record {field} as implemented",
            )
        errors.require(
            payload_implementation.get("native_reservation_before_task_commit") is True,
            "payload adapter must reserve native state before task commit",
        )
        errors.require(
            payload_implementation.get("start_failure_operand_preservation")
            == "implemented and failure-injected",
            "payload adapter must record failure-injected operand preservation",
        )
        errors.require(
            payload_implementation.get("terminal_acknowledgement")
            == PAYLOAD_TERMINAL_ACKNOWLEDGEMENT,
            "payload adapter must identify its native terminal acknowledgement",
        )
        errors.require(
            payload_implementation.get("engines") == PAYLOAD_ENGINES,
            "payload adapter engine assignment is not closed",
        )
        errors.require(
            payload_implementation.get("child_cleanup_handler")
            == "captures no R request and may be deferred until root close",
            "payload child cleanup must not retain R request resources",
        )
        errors.require(
            payload_implementation.get("flush_semantics")
            == "Dispatch I/O ordering barrier only; no durability claim",
            "payload flush must remain an ordering-only barrier",
        )
        errors.require(
            payload_implementation.get("flush_submission")
            == "serialized after terminal acknowledgement of every prior accepted payload operation",
            "payload flush must wait for prior terminal acknowledgements before submission",
        )
        errors.require(
            payload_implementation.get("flush_precommit_cancel_deadline")
            == "immediate terminal outcome before barrier submission",
            "payload flush must define pre-commit cancel/deadline",
        )
        errors.require(
            payload_implementation.get("payload_during_active_flush")
            == "queued in reserved same-direction submission order",
            "payload starts must be queued while a flush is active",
        )
        errors.require(
            payload_implementation.get("explicit_close_during_active_operation")
            == "cancel requests with an earlier submission sequence and wait for native acknowledgement",
            "payload close must cancel only earlier operations and wait for acknowledgement",
        )
        errors.require(
            payload_implementation.get("close_deadline")
            == "cleanup and view release always complete before timed_out publication; a terminal close failure known before start takes precedence",
            "payload close deadline must preserve cleanup and pre-existing failure precedence",
        )
        errors.require(
            payload_implementation.get("console_views")
            == "allocation-free independent Move-only Send+Sync retains",
            "payload console views must record allocation-free independent ownership",
        )
        errors.require(
            payload_implementation.get("console_root_lifecycle")
            == "runtime-owned roots survive view close/drop; hosted finish drains output and flush barriers, cancels input, waits for acknowledgement, then releases roots",
            "payload console root lifecycle must record hosted drain and acknowledgement",
        )
        errors.require(
            payload_implementation.get("same_direction_submission_order")
            == "reads serialize independently; writes and flushes share one ordered output queue",
            "payload adapter must record implemented per-direction submission ordering",
        )
        errors.require(
            payload_implementation.get("queued_cancellation_deadline")
            == "terminalize without payload submission, preserve owned buffer or shared owner, acknowledge exactly once and release the next request",
            "payload adapter must document queued cancellation and deadline handling",
        )
        errors.require(
            payload_implementation.get("low_level_relative_deadline")
            == PAYLOAD_LOW_LEVEL_RELATIVE_DEADLINE,
            "payload adapter must distinguish native relative timeouts from std.fs deadlines",
        )
        errors.require(
            payload_implementation.get("std_fs_shared_position_order")
            == STD_FS_SHARED_POSITION_ORDER,
            "std.fs payload operations must share the exact synchronized position FIFO",
        )
        errors.require(
            payload_implementation.get("std_fs_payload_handle") == STD_FS_PAYLOAD_HANDLE,
            "std.fs payload operations must use one file payload adapter handle",
        )
        errors.require(
            payload_implementation.get("std_fs_nonappend_positioning")
            == STD_FS_NONAPPEND_POSITIONING,
            "std.fs reads and non-append writes must pass their position to pread or pwrite",
        )
        errors.require(
            payload_implementation.get("std_fs_append_positioning")
            == STD_FS_APPEND_POSITIONING,
            "std.fs append writes must record O_APPEND and logical-position semantics",
        )
        errors.require(
            payload_implementation.get("std_fs_deadline_clock") == STD_FS_DEADLINE_CLOCK,
            "std.fs payload deadline must recompute the continuous-clock remainder and re-arm",
        )
        errors.require(
            payload_implementation.get("std_fs_absolute_deadline") == STD_FS_ABSOLUTE_DEADLINE,
            "std.fs payload deadlines must remain absolute across FIFO and native I/O",
        )
        errors.require(
            payload_implementation.get("std_fs_cancellation_deadline_acknowledgement")
            == STD_FS_CANCELLATION_DEADLINE_ACKNOWLEDGEMENT,
            "std.fs payload cancellation and deadline must retain ownership through native "
            "acknowledgement",
        )
        errors.require(
            payload_implementation.get("std_fs_explicit_close") == STD_FS_EXPLICIT_CLOSE,
            "std.fs explicit close must wait for handle root cleanup and propagate its error",
        )
        errors.require(
            string_set(payload_implementation.get("std_fs_whole_file_read_operations"))
            == STD_FS_WHOLE_FILE_READ_OPERATIONS,
            "std.fs whole-file read operation set is not closed",
        )
        errors.require(
            payload_implementation.get("std_fs_whole_file_read_pipeline")
            == STD_FS_WHOLE_FILE_READ_PIPELINE,
            "std.fs whole-file reads must record the exact native pipeline",
        )
        errors.require(
            payload_implementation.get("std_fs_whole_file_read_path_policy")
            == STD_FS_WHOLE_FILE_READ_PATH_POLICY,
            "std.fs whole-file reads must record ordinary and beneath symlink policy",
        )
        errors.require(
            payload_implementation.get("std_fs_whole_file_read_chunk_bytes") == 65536,
            "std.fs whole-file read chunk must be exactly 65536 bytes",
        )
        errors.require(
            payload_implementation.get("std_fs_whole_file_read_limit_eof")
            == STD_FS_WHOLE_FILE_READ_LIMIT_EOF,
            "std.fs whole-file reads must record limit-plus-one and EOF semantics",
        )
        errors.require(
            payload_implementation.get("std_fs_whole_file_read_terminal_acknowledgement")
            == STD_FS_WHOLE_FILE_READ_TERMINAL_ACKNOWLEDGEMENT,
            "std.fs whole-file reads must wait for descriptor close and handle root cleanup",
        )
        for field in (
            "task_runtime_integration",
            "public_std_fs_integration",
            "public_std_io_integration",
        ):
            errors.require(
                payload_implementation.get(field) is True,
                f"payload adapter must record {field} true",
            )
        errors.require(
            payload_implementation.get("profile_conformance_claim") is False,
            "partial payload adapter must keep profile_conformance_claim false",
        )
        errors.require(
            string_set(payload_implementation.get("public_std_fs_integrated_operations"))
            == PUBLIC_STD_FS_PAYLOAD_INTEGRATED_OPERATIONS,
            "payload adapter integrated std.fs operation set is not closed",
        )
        errors.require(
            string_set(payload_implementation.get("public_std_io_integrated_operations"))
            == PUBLIC_STD_IO_INTEGRATED_OPERATIONS,
            "payload adapter integrated std.io operation set is not closed",
        )
        errors.require(
            string_set(payload_implementation.get("public_std_io_unimplemented_operations"))
            == PUBLIC_STD_IO_UNIMPLEMENTED_OPERATIONS,
            "payload adapter unimplemented std.io operation set is not closed",
        )

    dns_resolver = async_record.get("dns_resolver_adapter")
    errors.require(isinstance(dns_resolver, dict), "dns_resolver_adapter must be an object")
    dns_implementation = (
        dns_resolver.get("implementation") if isinstance(dns_resolver, dict) else None
    )
    errors.require(
        isinstance(dns_implementation, dict),
        "DNS resolver adapter implementation must be an object",
    )
    if isinstance(dns_implementation, dict):
        errors.require(
            dns_implementation.get("status") == DNS_RESOLVER_STATUS,
            "DNS resolver adapter status must identify complete task-bridge integration",
        )
        errors.require(
            string_set(dns_implementation.get("cmake_targets"))
            == {"r_runtime_darwin_dns", "r_std_net"},
            "DNS resolver adapter CMake target set is not closed",
        )
        errors.require(
            string_set(dns_implementation.get("supported_operations"))
            == DNS_RESOLVER_OPERATIONS,
            "DNS resolver adapter operation set is not closed",
        )
        errors.require(
            dns_implementation.get("native_reservation_before_task_commit") is True,
            "DNS resolver adapter must reserve native state before task commit",
        )
        dns_contracts = (
            (
                "start_failure_operand_preservation",
                DNS_RESOLVER_START_PRESERVATION,
                "DNS resolver start preservation contract is not closed",
            ),
            (
                "task_local_validation",
                DNS_RESOLVER_TASK_VALIDATION,
                "DNS resolver validation contract is not closed",
            ),
            (
                "numeric_resolution",
                DNS_RESOLVER_NUMERIC_RESOLUTION,
                "DNS resolver numeric-path contract is not closed",
            ),
            (
                "dns_submission",
                DNS_RESOLVER_SUBMISSION,
                "DNS resolver submission contract is not closed",
            ),
            (
                "result_ordering",
                DNS_RESOLVER_RESULT_ORDERING,
                "DNS resolver result-ordering contract is not closed",
            ),
            (
                "error_mapping",
                DNS_RESOLVER_ERROR_MAPPING,
                "DNS resolver error-mapping contract is not closed",
            ),
            (
                "cancellation_deadline_acknowledgement",
                DNS_RESOLVER_TERMINAL_ACKNOWLEDGEMENT,
                "DNS resolver cancellation/deadline acknowledgement contract is not closed",
            ),
        )
        for field, expected, message in dns_contracts:
            errors.require(dns_implementation.get(field) == expected, message)
        errors.require(
            dns_implementation.get("profile_conformance_claim") is True,
            "complete DNS resolver adapter must claim its selected-profile contract",
        )

    network = async_record.get("nonblocking_socket_adapter")
    errors.require(isinstance(network, dict), "nonblocking_socket_adapter must be an object")
    network_implementation = network.get("implementation") if isinstance(network, dict) else None
    errors.require(
        isinstance(network_implementation, dict),
        "nonblocking socket adapter implementation must be an object",
    )
    if isinstance(network_implementation, dict):
        errors.require(
            network_implementation.get("status") == NETWORK_ADAPTER_STATUS,
            "network adapter status must identify integrated TCP shutdown and close support",
        )
        errors.require(
            string_set(network_implementation.get("cmake_targets"))
            == {"r_runtime_darwin_socket", "r_std_net"},
            "network adapter CMake target set is not closed",
        )
        errors.require(
            string_set(network_implementation.get("supported_operations"))
            == NETWORK_ADAPTER_OPERATIONS,
            "network adapter operation set is not closed",
        )
        errors.require(
            network_implementation.get("native_reservation_before_task_commit") is True,
            "network adapter must reserve native state before task commit",
        )
        errors.require(
            network_implementation.get("unix_domain_sockets") == NETWORK_UNIX_DOMAIN_SOCKETS,
            "Unix-domain socket contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_write_submission") == NETWORK_TCP_WRITE_SUBMISSION,
            "TCP write submission contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_write_completion") == NETWORK_TCP_WRITE_COMPLETION,
            "TCP write completion contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_write_ordering") == NETWORK_TCP_WRITE_ORDERING,
            "TCP write ordering and empty-buffer contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_write_cancellation_deadline_acknowledgement")
            == NETWORK_TCP_WRITE_TERMINAL_ACKNOWLEDGEMENT,
            "TCP write cancellation/deadline acknowledgement contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_shutdown_submission")
            == NETWORK_TCP_SHUTDOWN_SUBMISSION,
            "TCP shutdown submission and idempotence contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_shutdown_ordering") == NETWORK_TCP_SHUTDOWN_ORDERING,
            "TCP shutdown direction ordering contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_shutdown_commit_cancellation_deadline_acknowledgement")
            == NETWORK_TCP_SHUTDOWN_TERMINAL_ACKNOWLEDGEMENT,
            "TCP shutdown commit and acknowledgement contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_close_submission") == NETWORK_TCP_CLOSE_SUBMISSION,
            "TCP close submission and owner preservation contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_close_terminal_cleanup")
            == NETWORK_TCP_CLOSE_TERMINAL_CLEANUP,
            "TCP close terminal cleanup contract is not closed",
        )
        errors.require(
            network_implementation.get("tcp_close_cancellation_deadline_acknowledgement")
            == NETWORK_TCP_CLOSE_TERMINAL_ACKNOWLEDGEMENT,
            "TCP close cancellation/deadline acknowledgement contract is not closed",
        )
        errors.require(
            network_implementation.get("udp_datagram_submission_and_ownership")
            == NETWORK_UDP_DATAGRAM_SUBMISSION,
            "UDP datagram submission and ownership contract is not closed",
        )
        errors.require(
            network_implementation.get("udp_receive_buffer_contract")
            == NETWORK_UDP_RECEIVE_BUFFER_CONTRACT,
            "UDP receive buffer contract is not closed",
        )
        errors.require(
            network_implementation.get("udp_direction_ordering")
            == NETWORK_UDP_DIRECTION_ORDERING,
            "UDP direction ordering contract is not closed",
        )
        errors.require(
            network_implementation.get("udp_send_commit_cancellation_deadline_acknowledgement")
            == NETWORK_UDP_SEND_TERMINAL_ACKNOWLEDGEMENT,
            "UDP send commit and acknowledgement contract is not closed",
        )
        errors.require(
            network_implementation.get("udp_close_terminal_cleanup")
            == NETWORK_UDP_CLOSE_TERMINAL_CLEANUP,
            "UDP close terminal cleanup contract is not closed",
        )
        errors.require(
            network_implementation.get("profile_conformance_claim") is False,
            "partial network adapter must keep profile_conformance_claim false",
        )

    process_spawn = async_record.get("process_spawn_adapter")
    errors.require(isinstance(process_spawn, dict), "process_spawn_adapter must be an object")
    process_spawn_implementation = (
        process_spawn.get("implementation") if isinstance(process_spawn, dict) else None
    )
    errors.require(
        isinstance(process_spawn_implementation, dict),
        "process spawn adapter implementation must be an object",
    )
    if isinstance(process_spawn_implementation, dict):
        errors.require(
            process_spawn_implementation.get("status") == PROCESS_SPAWN_STATUS,
            "process adapter status must identify complete spawn, pipe, wait and terminate integration",
        )
        errors.require(
            string_set(process_spawn_implementation.get("cmake_targets"))
            == {"r_runtime_darwin_process", "r_std_process"},
            "process spawn adapter CMake target set is not closed",
        )
        errors.require(
            string_set(process_spawn_implementation.get("supported_operations"))
            == PROCESS_SPAWN_OPERATIONS,
            "process spawn adapter operation set is not closed",
        )
        errors.require(
            process_spawn_implementation.get("exact_program_invocation")
            == PROCESS_SPAWN_EXACT_INVOCATION,
            "process spawn adapter must preserve exact program invocation without PATH or shell",
        )
        errors.require(
            process_spawn_implementation.get("native_reservation_before_task_commit") is True,
            "process spawn adapter must reserve native state before task commit",
        )
        for field, expected in (
            ("start_failure_operand_preservation", PROCESS_SPAWN_START_PRESERVATION),
            ("creation_commit", PROCESS_SPAWN_CREATION_COMMIT),
            ("cancellation_deadline", PROCESS_SPAWN_CANCELLATION_DEADLINE),
            ("pipe_reservation", PROCESS_SPAWN_PIPE_RESERVATION),
            ("pipe_extraction", PROCESS_PIPE_EXTRACTION),
            ("wait_reservation", PROCESS_WAIT_RESERVATION),
            ("wait_completion", PROCESS_WAIT_COMPLETION),
            ("terminate_commit", PROCESS_TERMINATE_COMMIT),
            ("child_status_mapping", PROCESS_CHILD_STATUS_MAPPING),
            ("rollback", PROCESS_SPAWN_ROLLBACK),
            ("child_lifecycle", PROCESS_SPAWN_CHILD_LIFECYCLE),
            ("hosted_shutdown", PROCESS_SPAWN_HOSTED_SHUTDOWN),
        ):
            errors.require(
                process_spawn_implementation.get(field) == expected,
                f"process spawn adapter {field} contract is not closed",
            )
        errors.require(
            string_set(process_spawn_implementation.get("public_unimplemented_operations"))
            == PROCESS_SPAWN_UNIMPLEMENTED_OPERATIONS,
            "process spawn adapter unimplemented operation set is not closed",
        )
        errors.require(
            process_spawn_implementation.get("profile_conformance_claim") is True,
            "complete process adapter must claim its selected-profile contract",
        )

    matrix = async_record.get("capability_matrix")
    errors.require(isinstance(matrix, list), "capability_matrix must be an array")
    by_family: dict[str, dict[str, Any]] = {}
    if isinstance(matrix, list):
        for record in matrix:
            if not isinstance(record, dict) or not isinstance(record.get("family"), str):
                errors.messages.append("every capability record must have a string family")
                continue
            family = record["family"]
            if family in by_family:
                errors.messages.append(f"duplicate capability family: {family}")
                continue
            by_family[family] = record
    errors.require(set(by_family) == CAPABILITY_FAMILIES, "capability family set is not closed")

    lane_family = by_family.get("filesystem_metadata_cursor_namespace_durability")
    if lane_family is not None:
        errors.require(lane_family.get("lane") is True, "filesystem metadata must use the lane")
        errors.require(
            lane_family.get("cancellation") == "queued_or_acknowledged_after_return",
            "filesystem lane cancellation mode is invalid",
        )
        errors.require(
            lane_family.get("deadline") == "queued_or_acknowledged_after_return",
            "filesystem lane deadline mode is invalid",
        )
    for family in (
        "console_and_process_pipe_payload",
        "dns",
        "tcp_udp_establishment",
        "tcp_payload",
        "udp_datagram",
        "child_wait",
        "signal_wait",
    ):
        record = by_family.get(family)
        if record is None:
            continue
        errors.require(record.get("lane") is False, f"{family} must not use the filesystem lane")
        errors.require(record.get("cancellation") == "native", f"{family} cancellation must be native")
        errors.require(record.get("deadline") == "native", f"{family} deadline must be native")
    console_payload = by_family.get("console_and_process_pipe_payload")
    if console_payload is not None:
        errors.require(
            console_payload.get("acknowledgement") == "final Dispatch I/O handler",
            "console_and_process_pipe_payload must acknowledge at the final Dispatch I/O handler",
        )
    file_payload = by_family.get("file_payload_read_write")
    if file_payload is not None:
        errors.require(
            file_payload == FILE_PAYLOAD_CAPABILITY,
            "file_payload_read_write must record the file payload adapter and "
            "queued_or_acknowledged_after_return",
        )
    tcp_payload = by_family.get("tcp_payload")
    if tcp_payload is not None:
        errors.require(
            tcp_payload == TCP_PAYLOAD_CAPABILITY,
            "tcp_payload must acknowledge at the request terminal transition",
        )
    child_spawn = by_family.get("child_spawn")
    if child_spawn is not None:
        errors.require(child_spawn.get("lane") is False, "child spawn must not use the lane")
        errors.require(
            child_spawn.get("backend") == PROCESS_SPAWN_CAPABILITY_BACKEND,
            "child spawn capability must record exact-path suspended submission",
        )
        errors.require(
            child_spawn.get("cancellation") == "before_commit_only",
            "child spawn cancellation must be before_commit_only",
        )
        errors.require(
            child_spawn.get("deadline") == "before_commit_only",
            "child spawn deadline must be before_commit_only",
        )
        errors.require(
            child_spawn.get("acknowledgement") == PROCESS_SPAWN_CAPABILITY_ACKNOWLEDGEMENT,
            "child spawn acknowledgement must cover commit or rollback reap",
        )


BLOCKING_POOL_FIELDS = {
    "enabled": True,
    "thread_count": 4,
    "thread_class": "r.blocking",
    "disjoint_from_executor_workers": True,
    "disjoint_from_filesystem_adapter_lane": True,
    "may_execute_r_code": True,
    "executes": "entries passed to std.async::blocking (Library R-SLIB-ASYNC-0017)",
    "thread_start": "on demand while every existing pool thread is busy",
    "maximum_pending_calls": 65536,
    "queue_policy": "FIFO by committed submission sequence",
    "submission": "slot reserved before task commit; enqueue never allocates",
    "queued_cancellation": "remove before entry and destroy the staged arguments",
    "entered_cancellation": "acknowledge only after the entry returns and destroy its result",
    "cancellation_mode": "queued_removed_or_acknowledged_after_return",
}
BLOCKING_POOL_IMPLEMENTATION = {
    "cmake_target": "r_runtime_darwin_task",
    "source": "runtime/darwin/source/blocking_pool.inc",
    "worker_backend": "pthread",
    "worker_count_runtime_constant": "R_RUNTIME_BLOCKING_THREAD_COUNT",
    "pending_bound_runtime_constant": "R_RUNTIME_BLOCKING_MAX_PENDING_CALLS",
}


def validate_blocking_pool(
    manifest: dict[str, Any], root: Path, errors: ValidationErrors
) -> None:
    """Core R-TERM-0015 and Library R-SLIB-ASYNC-0017: the blocking call pool record is closed
    and its thread count and pending bound are the runtime's own constants."""
    async_record = manifest.get("hosted_native_async")
    if not isinstance(async_record, dict):
        return
    pool = async_record.get("blocking_call_pool")
    errors.require(isinstance(pool, dict), "blocking_call_pool must be an object")
    if not isinstance(pool, dict):
        return
    errors.require(
        set(pool) == set(BLOCKING_POOL_FIELDS) | {"implementation"},
        "blocking call pool record key set is not closed",
    )
    for field, expected in BLOCKING_POOL_FIELDS.items():
        errors.require(
            pool.get(field) == expected, f"blocking call pool {field} must be {expected!r}"
        )
    errors.require(
        pool.get("implementation") == BLOCKING_POOL_IMPLEMENTATION,
        "blocking call pool implementation record is not closed",
    )
    header = root / "runtime/include/r_runtime_task.h"
    try:
        text = header.read_text(encoding="utf-8")
    except OSError:
        errors.require(False, f"blocking call pool runtime header is missing: {header}")
        return
    errors.require(
        f"#define R_RUNTIME_BLOCKING_THREAD_COUNT {BLOCKING_POOL_FIELDS['thread_count']}U"
        in text,
        "runtime blocking pool thread count differs from the manifest",
    )
    errors.require(
        "#define R_RUNTIME_BLOCKING_MAX_PENDING_CALLS "
        f"((size_t){BLOCKING_POOL_FIELDS['maximum_pending_calls']}U)" in text,
        "runtime blocking pool pending bound differs from the manifest",
    )


FILE_PAYLOAD_CAPABILITY = {
    "family": "file_payload_read_write",
    "backend": (
        "file payload adapter for regular files: pread, pwrite and write as work items of a "
        "private libdispatch concurrent queue with a bounded admission count; other file types "
        "opened through std.fs use Dispatch I/O"
    ),
    "lane": False,
    "cancellation": "queued_or_acknowledged_after_return",
    "deadline": "queued_or_acknowledged_after_return",
    "acknowledgement": "native call return and request terminal transition",
}
TCP_PAYLOAD_CAPABILITY = {
    "family": "tcp_payload",
    "backend": (
        "non-blocking BSD stream sockets: immediate system calls, then per-handle "
        "DispatchSourceRead/DispatchSourceWrite readiness"
    ),
    "lane": False,
    "cancellation": "native",
    "deadline": "native",
    "acknowledgement": "request terminal transition after the last nonblocking system call",
}
FILE_PAYLOAD_ADAPTER_FIELDS = {
    "enabled": True,
    "maximum_entered_transfers": 4,
    "executes": "regular-file payload byte transfer of std.fs (Library R-SLIB-ASYNC-0019)",
    "transfer_operations": [
        "std.fs::read",
        "std.fs::write",
        "std.fs::write_all",
        "std.fs::read_at",
        "std.fs::write_all_at",
        "std.fs::read_file",
        "std.fs::read_file_beneath",
        "std.fs::write_file_atomic_no_replace",
        "std.fs::write_file_atomic_no_replace_beneath",
    ],
    "native_entries": ["pread", "pwrite", "write"],
    "worker_backend": (
        "work items of the private libdispatch concurrent queue r.io.file targeting the "
        "default-QoS global queue"
    ),
    "disjoint_from_executor_workers": True,
    "disjoint_from_filesystem_adapter_lane": True,
    "disjoint_from_blocking_call_pool": True,
    "may_execute_r_code": False,
    "admission_policy": (
        "admitted at activation while fewer than maximum_entered_transfers are admitted, "
        "otherwise queued in one process-wide FIFO by activation order; a finishing work item "
        "takes the FIFO head on its own thread"
    ),
    "queued_cancellation": (
        "a transfer waiting for admission is removed and finished on its handle serial queue "
        "without a native call; an admitted transfer that has not entered its call finishes "
        "without it"
    ),
    "entered_cancellation": (
        "record the event, make no further native call and acknowledge after the call returns"
    ),
    "cancellation_mode": "queued_or_acknowledged_after_return",
    "retention": "request, buffer, handle and task frame through the native return",
    "descriptor_release": (
        "root cleanup of a closed or dropped handle runs after its last admitted transfer "
        "finished"
    ),
}
FILE_PAYLOAD_ADAPTER_IMPLEMENTATION = {
    "cmake_target": "r_runtime_darwin_io",
    "source": "runtime/darwin/source/io_direct.c",
    "bound_runtime_constant": "R_RUNTIME_DARWIN_IO_FILE_MAX_ENTERED_TRANSFERS",
}


def validate_file_payload_adapter(
    manifest: dict[str, Any], root: Path, errors: ValidationErrors
) -> None:
    """Core R-TERM-0016 and Library R-SLIB-ASYNC-0019: the file payload adapter record is closed
    and its admission bound is the runtime's own constant."""
    async_record = manifest.get("hosted_native_async")
    if not isinstance(async_record, dict):
        return
    adapter = async_record.get("file_payload_adapter")
    errors.require(isinstance(adapter, dict), "file_payload_adapter must be an object")
    if not isinstance(adapter, dict):
        return
    errors.require(
        set(adapter) == set(FILE_PAYLOAD_ADAPTER_FIELDS) | {"implementation"},
        "file payload adapter record key set is not closed",
    )
    for field, expected in FILE_PAYLOAD_ADAPTER_FIELDS.items():
        errors.require(
            adapter.get(field) == expected, f"file payload adapter {field} must be {expected!r}"
        )
    errors.require(
        adapter.get("implementation") == FILE_PAYLOAD_ADAPTER_IMPLEMENTATION,
        "file payload adapter implementation record is not closed",
    )
    source = root / FILE_PAYLOAD_ADAPTER_IMPLEMENTATION["source"]
    try:
        text = source.read_text(encoding="utf-8")
    except OSError:
        errors.require(False, f"file payload adapter source is missing: {source}")
        return
    errors.require(
        "#define R_RUNTIME_DARWIN_IO_FILE_MAX_ENTERED_TRANSFERS "
        f"{FILE_PAYLOAD_ADAPTER_FIELDS['maximum_entered_transfers']}U" in text,
        "runtime file payload admission bound differs from the manifest",
    )


def parse_arguments() -> argparse.Namespace:
    default_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=default_root)
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--core-specification", type=Path)
    parser.add_argument("--library-specification", type=Path)
    parser.add_argument("--core-inventory", type=Path)
    parser.add_argument("--library-inventory", type=Path)
    return parser.parse_args()


def validate_freestanding_manifest(
    root: Path,
    manifest: dict[str, Any],
    core_inventory: dict[str, Any],
    library_inventory: dict[str, Any],
    errors: ValidationErrors,
) -> int:
    """Validate the freestanding manifest against Core R-CONF-0005 and the hosted twin."""
    hosted = load_object(root / HOSTED_MANIFEST_PATH)
    hosted_core = hosted.get("core") if isinstance(hosted.get("core"), dict) else {}
    identity = manifest.get("identity")
    errors.require(isinstance(identity, dict), "manifest identity must be an object")
    if isinstance(identity, dict):
        errors.require(identity.get("target_triple") == "arm64-apple-darwin", "wrong target triple")
        errors.require(
            identity.get("name") == "arm64-apple-darwin-freestanding", "wrong freestanding name"
        )
        errors.require(
            identity.get("core_specification_revision")
            == core_inventory.get("generated_from", {}).get("revision"),
            "target/Core inventory revision mismatch",
        )
        errors.require(
            identity.get("standard_library_specification_revision")
            == library_inventory.get("generated_from", {}).get("revision"),
            "target/Library inventory revision mismatch",
        )
        hosted_identity = hosted.get("identity") if isinstance(hosted.get("identity"), dict) else {}
        errors.require(
            identity.get("minimum_os_version") == hosted_identity.get("minimum_os_version"),
            "freestanding minimum OS version must match the hosted target",
        )
    errors.require(manifest.get("schema") == "r-target-manifest-0.1", "wrong manifest schema")
    errors.require(manifest.get("manifest_revision") == 10, "target manifest revision must be 10")
    errors.require(
        manifest.get("status") == "draft-implementation-contract",
        "freestanding target must remain a draft implementation contract",
    )
    errors.require(
        manifest.get("conformance_claim") is False,
        "draft target must not claim implementation conformance",
    )
    errors.require(
        manifest.get("toolchain") == DARWIN_TOOLCHAIN_CONTRACT,
        "freestanding toolchain contract must equal the hosted Darwin toolchain",
    )
    core = manifest.get("core")
    errors.require(isinstance(core, dict), "core target record must be an object")
    if isinstance(core, dict):
        for field in FREESTANDING_SHARED_CORE_FIELDS:
            errors.require(
                field in core and core.get(field) == hosted_core.get(field),
                f"core.{field} must equal the hosted target record",
            )
        validate_c_abi_numeric_types(core, errors)
        errors.require(
            core.get("panic") == FREESTANDING_PANIC_CONTRACT,
            "freestanding panic contract is not closed",
        )
        errors.require(
            core.get("stack") == FREESTANDING_STACK_CONTRACT,
            "freestanding stack contract is not closed",
        )
        errors.require(
            core.get("allocator") == FREESTANDING_ALLOCATOR_CONTRACT,
            "freestanding allocator contract is not closed",
        )
        errors.require(
            core.get("threads") == FREESTANDING_THREADS_CONTRACT,
            "freestanding threads contract is not closed",
        )
        errors.require(
            core.get("floating_environment") == FREESTANDING_FLOATING_CONTRACT,
            "freestanding floating-environment contract is not closed",
        )
        expected_keys = set(FREESTANDING_SHARED_CORE_FIELDS) | {
            "panic",
            "stack",
            "allocator",
            "threads",
            "floating_environment",
        }
        errors.require(set(core) == expected_keys, "freestanding core record key set is not closed")
    errors.require(
        manifest.get("freestanding") == FREESTANDING_CONTRACT,
        "freestanding environment contract is not closed",
    )
    errors.require(
        "hosted_native_async" not in manifest and "standard_library" not in manifest,
        "freestanding manifest must not carry hosted records",
    )
    errors.require(
        set(manifest)
        == {
            "schema",
            "manifest_revision",
            "status",
            "conformance_claim",
            "identity",
            "toolchain",
            "core",
            "freestanding",
        },
        "freestanding manifest key set is not closed",
    )
    if errors.messages:
        for message in errors.messages:
            print(f"target manifest error: {message}", file=sys.stderr)
        return 1
    print(
        "target manifest valid: arm64-apple-darwin freestanding, "
        "498 Core rules, 476 Library rules, environment panic handler, "
        "environment stack bounds, no allocator"
    )
    return 0


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    manifest_path = arguments.manifest or (
        root / "targets/arm64-apple-darwin.hosted-native-async.json"
    )
    core_specification = arguments.core_specification or (
        root / "specification/R_LANGUAGE_SPECIFICATION_0_1.en.adoc"
    )
    library_specification = arguments.library_specification or (
        root / "specification/R_STANDARD_LIBRARY_SPECIFICATION_0_1.en.adoc"
    )
    core_inventory_path = arguments.core_inventory or (
        root / "specification/generated/R_LANGUAGE_SPECIFICATION_0_1.rules.json"
    )
    library_inventory_path = arguments.library_inventory or (
        root / "specification/generated/R_STANDARD_LIBRARY_SPECIFICATION_0_1.rules.json"
    )

    try:
        errors = ValidationErrors()
        manifest = load_object(manifest_path)
        core_inventory = validate_catalog(
            core_specification,
            core_inventory_path,
            CORE_REQUIRED_RULES,
            498,
            errors,
        )
        library_inventory = validate_catalog(
            library_specification,
            library_inventory_path,
            LIBRARY_REQUIRED_RULES,
            476,
            errors,
        )
        identity = manifest.get("identity")
        if isinstance(identity, dict) and identity.get("profile") == FREESTANDING_PROFILE:
            return validate_freestanding_manifest(
                root, manifest, core_inventory, library_inventory, errors
            )
        errors.require(isinstance(identity, dict), "manifest identity must be an object")
        if isinstance(identity, dict):
            errors.require(identity.get("target_triple") == "arm64-apple-darwin", "wrong target triple")
            errors.require(identity.get("profile") == "hosted-native-async", "wrong profile")
            errors.require(
                identity.get("core_specification_revision")
                == core_inventory.get("generated_from", {}).get("revision"),
                "target/Core inventory revision mismatch",
            )
            errors.require(
                identity.get("standard_library_specification_revision")
                == library_inventory.get("generated_from", {}).get("revision"),
                "target/Library inventory revision mismatch",
            )
        errors.require(manifest.get("schema") == "r-target-manifest-0.1", "wrong manifest schema")
        errors.require(
            manifest.get("manifest_revision") == 10,
            "target manifest revision must be 10",
        )
        errors.require(
            manifest.get("status") == "draft-implementation-contract",
            "first target must remain a draft implementation contract",
        )
        errors.require(
            manifest.get("conformance_claim") is False,
            "draft target must not claim implementation conformance",
        )
        toolchain = manifest.get("toolchain")
        errors.require(isinstance(toolchain, dict), "toolchain must be an object")
        if isinstance(toolchain, dict):
            errors.require(toolchain.get("c_compiler") == "Apple clang", "wrong C compiler")
            errors.require(toolchain.get("c_compiler_version") == "21.0.0", "wrong Apple Clang version")
            errors.require(
                toolchain.get("c_compiler_build") == "clang-2100.3.34.2",
                "wrong Apple Clang build",
            )
            errors.require(toolchain.get("sdk") == "macOS 27.0", "wrong macOS SDK")
            errors.require(
                toolchain.get("generated_application_language") == "ISO C17",
                "generated application language must be ISO C17",
            )
            errors.require(
                toolchain.get("generated_application_extensions") is False,
                "generated application C must not use extensions",
            )
            errors.require(
                toolchain == DARWIN_TOOLCHAIN_CONTRACT,
                "Darwin toolchain contract is not closed",
            )
        core = manifest.get("core")
        errors.require(isinstance(core, dict), "core target record must be an object")
        if isinstance(core, dict):
            errors.require(core.get("data_model") == "LP64", "Darwin target must use LP64")
            errors.require(core.get("byte_order") == "little", "Darwin target must be little-endian")
            errors.require(core.get("usize_width") == 64, "usize must be 64-bit")
            errors.require(core.get("isize_width") == 64, "isize must be 64-bit")
            process_status = core.get("process_status", {})
            errors.require(process_status.get("application_range") == [0, 111],
                           "main application statuses must be 0..111")
            errors.require(process_status.get("main_error_reserved_range") == [112, 124],
                           "main error statuses must reserve 112..124")
            errors.require(process_status.get("invalid_main_status") == 124,
                           "invalid main status must map to 124")
            errors.require(process_status.get("implicit_main_error_domains") == {
                "allocation": 112, "io": 113, "filesystem": 113, "network": 114,
                "conversion": 115, "format": 115, "async_runtime": 116, "threading": 116,
                "math": 117, "time": 117, "environment": 117, "process": 117,
                "bytes": 117, "string": 117, "c_abi": 117,
            }, "implicit main portable-domain status mapping must be exhaustive and stable")
            validate_c_abi_numeric_types(core, errors)
            errors.require(
                core.get("checked_errors") == CHECKED_ERROR_CONTRACT,
                "checked-error carrier contract is not closed",
            )
            panic = core.get("panic")
            errors.require(
                isinstance(panic, dict) and panic.get("strategy") == "unwind",
                "first target panic strategy must be unwind",
            )
            errors.require(
                panic == HOSTED_PANIC_CONTRACT,
                "hosted panic contract is not closed",
            )
            errors.require(
                core.get("stack") == DARWIN_STACK_CONTRACT,
                "Darwin stack contract is not closed",
            )
            allocator = core.get("allocator")
            errors.require(isinstance(allocator, dict), "allocator contract must be an object")
            if isinstance(allocator, dict):
                errors.require(
                    allocator.get("maximum_object_size")
                    == MD5_SHA_1_256_MAXIMUM_OBJECT_SIZE,
                    "maximum object size must satisfy the MD5/SHA-1/SHA-256 message-length domain",
                )
        standard_library = manifest.get("standard_library")
        errors.require(
            isinstance(standard_library, dict),
            "standard_library target record must be an object",
        )
        if isinstance(standard_library, dict):
            errors.require(
                string_set(standard_library.get("reserved_beneath_components"))
                == {".r-dir-stage-*"},
                "reserved beneath component set is not closed",
            )
            errors.require(
                standard_library.get("atomic_no_replace") == ATOMIC_NO_REPLACE_CONTRACT,
                "atomic whole-file no-replace contract is not closed",
            )
        validate_lane(manifest, errors)
        validate_blocking_pool(manifest, root, errors)
        validate_file_payload_adapter(manifest, root, errors)
        if errors.messages:
            for message in errors.messages:
                print(f"target manifest error: {message}", file=sys.stderr)
            return 1
        print(
            "target manifest valid: arm64-apple-darwin hosted-native-async, "
            "498 Core rules, 476 Library rules, 4 filesystem-lane threads, "
            "4 blocking-pool threads, 262144-byte generated-frame ceiling"
        )
        return 0
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"target manifest check failed: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
