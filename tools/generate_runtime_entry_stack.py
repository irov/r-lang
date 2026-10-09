#!/usr/bin/env python3
"""Validate and materialize direct-entry stack measurements for the Darwin target."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from pathlib import Path
from typing import Any, Iterable


SCHEMA = "r-runtime-entry-stack-inventory-0.1"
INVENTORY_REVISION = 22
TARGET_MANIFEST = "targets/arm64-apple-darwin.hosted-native-async.json"
TARGET_NAME = "arm64-apple-darwin-hosted-native-async"
TARGET_MANIFEST_REVISION = 11
COMPILER = "Homebrew clang"
COMPILER_VERSION = "22.1.8"
COMPILER_BUILD = "sha256:68bb87f784f09da01b7aea0f70952fb23f606242cdc4f6ff3cc26c372af0706f"
FRAME_CEILING_BYTES = 262144
MAXIMUM_INPUT_REPORTS = 128
MAXIMUM_INPUT_RECORDS = 4096
MAXIMUM_INPUT_BYTES_PER_REPORT = 16 * 1024 * 1024

COMPILE_FLAGS = (
    "-std=c17",
    "-O0",
    "-fstack-usage",
    "-fno-inline-functions",
    "-fno-lto",
)

EXTERNAL_ENTRIES = (
    # Hosted lifecycle.
    ("r_runtime_hosted_allocator", "runtime/source/hosted_entry.c", "runtime-hosted"),
    (
        "r_runtime_hosted_argument_snapshot",
        "runtime/source/hosted_entry.c",
        "runtime-hosted",
    ),
    (
        "r_runtime_hosted_async_root_start_failure",
        "runtime/source/hosted_entry.c",
        "runtime-hosted",
    ),
    ("r_runtime_emergency_write", "runtime/source/hosted_entry.c", "runtime-hosted"),
    ("r_runtime_hosted_after_object_drop", "runtime/source/hosted_entry.c", "runtime-hosted"),
    ("r_runtime_hosted_drain", "runtime/source/hosted_entry.c", "runtime-hosted"),
    ("r_runtime_report_main_error", "runtime/source/hosted_entry.c", "runtime-hosted"),
    ("r_runtime_hosted_finish", "runtime/source/hosted_entry.c", "runtime-hosted"),
    ("r_runtime_hosted_start", "runtime/source/hosted_entry.c", "runtime-hosted"),
    (
        "r_runtime_thread_detach",
        "runtime/source/thread_attachment.c",
        "type-glue-direct",
    ),
    ("r_runtime_c_call_begin", "runtime/source/thread_attachment.c", "runtime-c-boundary"),
    ("r_runtime_c_call_end", "runtime/source/thread_attachment.c", "runtime-c-boundary"),
    ("r_runtime_c_entry_begin", "runtime/source/thread_attachment.c", "runtime-c-boundary"),
    ("r_runtime_c_entry_end", "runtime/source/thread_attachment.c", "runtime-c-boundary"),
    # Stack and panic boundaries.
    ("r_runtime_panic", "runtime/source/panic_abort.c", "runtime-panic"),
    # L39 (Core R-ERR-0005): the unwind state of a thread.
    ("r_runtime_raise", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_raise_text", "runtime/source/panic_abort.c", "runtime-panic"),
    (
        "r_runtime_unwinding_current_thread",
        "runtime/source/panic_abort.c",
        "type-glue-direct",
    ),
    # Lock guards of std.sync poison their lock when dropped during a panic (R-LIB-0014).
    ("r_runtime_panicking", "runtime/source/panic_abort.c", "type-glue-direct"),
    ("r_runtime_unwind_cleanup_enter", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_unwind_cleanup_leave", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_unwind_terminate", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_task_panic_park", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_panic_unpark", "runtime/darwin/source/task.c", "runtime-task"),
    (
        "r_runtime_stack_initialize_current_thread",
        "runtime/darwin/source/stack.c",
        "runtime-stack",
    ),
    ("r_runtime_stack_require", "runtime/darwin/source/stack.c", "runtime-stack"),
    # Unique and shared owners.
    ("r_runtime_own_adopt", "runtime/source/own.c", "runtime-owner"),
    ("r_runtime_own_create", "runtime/source/own.c", "runtime-owner"),
    ("r_runtime_own_create_initialize", "runtime/source/own.c", "runtime-owner"),
    ("r_runtime_own_get", "runtime/source/own.c", "runtime-owner"),
    ("r_runtime_own_get_mut", "runtime/source/own.c", "runtime-owner"),
    ("r_runtime_own_into_raw", "runtime/source/own.c", "runtime-owner"),
    ("r_runtime_own_release", "runtime/source/own.c", "runtime-owner"),
    ("r_runtime_drop_iterative", "runtime/source/drop.c", "runtime-owner"),
    ("r_runtime_arc_create", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_get", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_release", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_weak_arc_release", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_rc_create", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_get", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_release", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_weak_rc_release", "runtime/source/rc.c", "runtime-owner"),
    # R-OWN-0020 (L26): generated clone glue copies owners and takes new shared handles.
    ("r_runtime_arc_clone", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_weak_arc_clone", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_rc_clone", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_weak_rc_clone", "runtime/source/rc.c", "runtime-owner"),
    # Runtime-backed container destruction emitted by type glue.
    ("r_json_cursor_list_push", "library/internal/json/source/collections.c", "stdlib-operation"),
    ("r_json_cursor_dict_initialize", "library/internal/json/source/collections.c", "stdlib-operation"),
    ("r_json_cursor_dict_insert", "library/internal/json/source/collections.c", "stdlib-operation"),
    ("r_runtime_list_initialize", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_iter", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_next", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_dict_iter", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_next", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_array_destroy", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_initialize", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_dict_destroy", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_list_destroy", "runtime/source/list.c", "runtime-container"),
    # R-OWN-0020 (L26): generated clone glue builds copies of containers and strings.
    ("r_runtime_array_with_capacity", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_list_push_back", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_dict_with_capacity", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_insert", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_string_from_valid_utf8", "runtime/source/string.c", "runtime-container"),
    # Eager/resumable task ABI.
    ("r_runtime_task_scope_open", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    ("r_runtime_task_scope_reserve", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    ("r_runtime_task_scope_abandon", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    ("r_runtime_task_scope_bind", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    ("r_runtime_task_scope_wait", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    ("r_runtime_task_scope_wait_until", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    ("r_runtime_task_scope_wait_vacancy", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    (
        "r_runtime_task_scope_wait_vacancy_until",
        "runtime/darwin/source/task_scope.inc",
        "runtime-task",
    ),
    ("r_runtime_task_scope_cancel", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    ("r_runtime_task_scope_drop", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    ("r_runtime_task_scope_close", "runtime/darwin/source/task_scope.inc", "runtime-task"),
    (
        "r_runtime_task_await",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_destroy",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_execution_await",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_execution_cancel_requested",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_resumable_start_prepare",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_start_commit_initialize",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_start_commit_initialize_inline",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_start_commit_initialize_deferred",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    ("r_runtime_task_run_deferred", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_direct_begin", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_direct_end", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_inline_completion_allowed", "runtime/darwin/source/task.c", "runtime-task"),
    # Core R-STMT-0019: deadline blocks and the deadline of a starting task.
    ("r_runtime_task_deadline_enter", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_deadline_leave", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_deadline_narrow", "runtime/darwin/source/task.c", "runtime-task"),
    # Core R-STMT-0020: budget blocks.
    ("r_runtime_task_budget_enter", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_budget_leave", "runtime/darwin/source/task.c", "runtime-task"),
    # Standard-library operations currently selected by strict-C17 lowering.
    ("r_std_alloc_bytes", "library/std/alloc/source/bytes.c", "stdlib-operation"),
    ("r_std_alloc_into_value", "library/std/alloc/source/into_value.c", "stdlib-operation"),
    ("r_std_alloc_try_new", "library/std/alloc/source/try_new.c", "stdlib-operation"),
    ("r_std_arc_clone", "library/std/arc/source/clone.c", "stdlib-operation"),
    ("r_std_array_capacity", "library/std/array/source/capacity.c", "stdlib-operation"),
    ("r_std_array_clear", "library/std/array/source/clear.c", "stdlib-operation"),
    ("r_std_array_create", "library/std/array/source/create.c", "stdlib-operation"),
    ("r_std_array_filled", "library/std/array/source/filled.c", "stdlib-operation"),
    ("r_std_array_push", "library/std/array/source/push.c", "stdlib-operation"),
    ("r_std_array_remove", "library/std/array/source/remove.c", "stdlib-operation"),
    ("r_std_array_reserve", "library/std/array/source/reserve.c", "stdlib-operation"),
    (
        "r_std_array_with_capacity",
        "library/std/array/source/with_capacity.c",
        "stdlib-operation",
    ),
    ("r_std_dict_clear", "library/std/dict/source/clear.c", "stdlib-operation"),
    ("r_std_dict_create", "library/std/dict/source/create.c", "stdlib-operation"),
    ("r_std_dict_insert", "library/std/dict/source/insert.c", "stdlib-operation"),
    ("r_std_dict_iter", "library/std/dict/source/iter.c", "stdlib-operation"),
    ("r_std_dict_next", "library/std/dict/source/next.c", "stdlib-operation"),
    ("r_std_dict_remove", "library/std/dict/source/remove.c", "stdlib-operation"),
    ("r_std_dict_reserve", "library/std/dict/source/reserve.c", "stdlib-operation"),
    (
        "r_std_dict_with_capacity",
        "library/std/dict/source/with_capacity.c",
        "stdlib-operation",
    ),
    ("r_std_list_clear", "library/std/list/source/clear.c", "stdlib-operation"),
    ("r_std_list_create", "library/std/list/source/create.c", "stdlib-operation"),
    ("r_std_list_iter", "library/std/list/source/iter.c", "stdlib-operation"),
    ("r_std_list_next", "library/std/list/source/next.c", "stdlib-operation"),
    ("r_std_async_cancel", "library/std/async/source/cancel.c", "stdlib-operation"),
    ("r_std_async_detach", "library/std/async/source/detach.c", "stdlib-operation"),
    ("r_std_async_join", "library/std/async/source/join.c", "stdlib-operation"),
    ("r_std_async_task_id", "library/std/async/source/task_id.c", "stdlib-operation"),
    ("r_std_c_checked", "library/std/c/source/checked.c", "stdlib-operation"),
    (
        "r_std_c_link_available",
        "library/std/c/source/link_available.c",
        "stdlib-operation",
    ),
    ("r_std_c_target", "library/std/c/source/target.c", "stdlib-operation"),
    (
        "r_std_c_string_from_str",
        "library/std/c/source/string_from_str.c",
        "stdlib-operation",
    ),
    (
        "r_std_c_string_as_slice",
        "library/std/c/source/string_as_slice.c",
        "stdlib-operation",
    ),
    (
        "r_std_c_string_as_ptr",
        "library/std/c/source/string_as_ptr.c",
        "stdlib-operation",
    ),
    ("r_std_c_validate_utf8", "library/std/c/source/validate_utf8.c", "stdlib-operation"),
    ("r_std_c_copy_utf8", "library/std/c/source/copy_utf8.c", "stdlib-operation"),
    ("r_std_c_attach_thread", "library/std/c/source/attach_thread.c", "stdlib-operation"),
    ("r_std_c_adopt_handle", "library/std/c/source/adopt_handle.c", "stdlib-operation"),
    ("r_std_c_detach_thread", "library/std/c/source/detach_thread.c", "stdlib-operation"),
    ("r_std_c_handle_pointer", "library/std/c/source/handle_pointer.c", "stdlib-operation"),
    ("r_std_c_release_handle", "library/std/c/source/release_handle.c", "stdlib-operation"),
    (
        "r_std_convert_checked",
        "library/std/convert/source/checked.c",
        "stdlib-operation",
    ),
    ("r_std_bytes_append", "library/std/bytes/source/append.c", "stdlib-operation"),
    (
        "r_std_bytes_append_u16_le",
        "library/std/bytes/source/append_u16_le.c",
        "stdlib-operation",
    ),
    (
        "r_std_bytes_append_u32_le",
        "library/std/bytes/source/append_u32_le.c",
        "stdlib-operation",
    ),
    (
        "r_std_bytes_append_u64_le",
        "library/std/bytes/source/append_u64_le.c",
        "stdlib-operation",
    ),
    ("r_std_bytes_append_u8", "library/std/bytes/source/append_u8.c", "stdlib-operation"),
    ("r_std_bytes_equal", "library/std/bytes/source/equal.c", "stdlib-operation"),
    ("r_std_hash_crc32", "library/std/hash/source/crc32.c", "stdlib-operation"),
    ("r_std_hash_md5", "library/std/hash/source/md5.c", "stdlib-operation"),
    ("r_std_hash_sha1", "library/std/hash/source/sha1.c", "stdlib-operation"),
    ("r_std_hash_sha256", "library/std/hash/source/sha256.c", "stdlib-operation"),
    ("r_std_hash_sha512", "library/std/hash/source/sha512.c", "stdlib-operation"),
    (
        "r_std_bytes_with_capacity",
        "library/std/bytes/source/with_capacity.c",
        "stdlib-operation",
    ),
    (
        "r_std_bits_align_byte",
        "library/std/bits/source/align_byte.c",
        "stdlib-operation",
    ),
    ("r_std_bits_read", "library/std/bits/source/read.c", "stdlib-operation"),
    ("r_std_secret_as_slice", "library/std/secret/source/as_slice.c", "stdlib-operation"),
    (
        "r_std_secret_as_slice_mut",
        "library/std/secret/source/as_slice_mut.c",
        "stdlib-operation",
    ),
    (
        "r_std_secret_constant_time_equal",
        "library/std/secret/source/constant_time_equal.c",
        "stdlib-operation",
    ),
    ("r_std_secret_from_bytes", "library/std/secret/source/from_bytes.c", "stdlib-operation"),
    ("r_std_secret_len", "library/std/secret/source/len.c", "stdlib-operation"),
    ("r_std_secret_with_length", "library/std/secret/source/with_length.c", "stdlib-operation"),
    ("r_std_secret_zeroize", "library/std/secret/source/zeroize.c", "stdlib-operation"),
    ("r_std_random_fill", "library/std/random/source/fill.c", "stdlib-operation"),
    ("r_std_test_allocation_attempts", "library/std/test/source/allocation_attempts.c", "stdlib-operation"),
    ("r_std_test_fail_allocation_at", "library/std/test/source/fail_allocation_at.c", "stdlib-operation"),
    (
        "r_std_utf8_is_valid",
        "library/std/utf8/source/is_valid.c",
        "stdlib-operation",
    ),
    (
        "r_std_utf8_validate",
        "library/std/utf8/source/validate.c",
        "stdlib-operation",
    ),
    ("r_std_fs_as_error", "library/std/fs/source/as_error.c", "stdlib-operation"),
    (
        "r_std_fs_file_metadata",
        "library/std/fs/source/file_metadata.c",
        "stdlib-operation",
    ),
    (
        "r_std_fs_create_directory_beneath",
        "library/std/fs/source/create_directory_beneath.c",
        "stdlib-operation",
    ),
    (
        "r_std_fs_open_directory",
        "library/std/fs/source/open_directory.c",
        "stdlib-operation",
    ),
    ("r_std_fs_open_file", "library/std/fs/source/open_file.c", "stdlib-operation"),
    (
        "r_std_fs_path_from_utf8",
        "library/std/fs/source/path_from_utf8.c",
        "stdlib-operation",
    ),
    (
        "r_std_fs_path_from_utf8_bytes",
        "library/std/fs/source/path_from_utf8_bytes.c",
        "stdlib-operation",
    ),
    (
        "r_std_fs_path_is_absolute",
        "library/std/fs/source/path_is_absolute.c",
        "stdlib-operation",
    ),
    ("r_std_fs_close_directory", "library/std/fs/source/close_directory.c", "stdlib-operation"),
    ("r_std_fs_close_file", "library/std/fs/source/close_file.c", "stdlib-operation"),
    ("r_std_fs_create_directory", "library/std/fs/source/create_directory.c", "stdlib-operation"),
    ("r_std_fs_flush", "library/std/fs/source/flush.c", "stdlib-operation"),
    ("r_std_fs_iterate", "library/std/fs/source/iterate.c", "stdlib-operation"),
    ("r_std_fs_metadata", "library/std/fs/source/metadata.c", "stdlib-operation"),
    ("r_std_fs_metadata_beneath", "library/std/fs/source/metadata_beneath.c", "stdlib-operation"),
    ("r_std_fs_next", "library/std/fs/source/next.c", "stdlib-operation"),
    ("r_std_fs_open_directory_beneath", "library/std/fs/source/open_directory_beneath.c", "stdlib-operation"),
    ("r_std_fs_open_file_beneath", "library/std/fs/source/open_file_beneath.c", "stdlib-operation"),
    ("r_std_fs_read_file_beneath", "library/std/fs/source/read_file_beneath.c", "stdlib-operation"),
    ("r_std_fs_remove_directory_beneath", "library/std/fs/source/remove_directory_beneath.c", "stdlib-operation"),
    ("r_std_fs_remove_file_beneath", "library/std/fs/source/remove_file_beneath.c", "stdlib-operation"),
    ("r_std_fs_rename_beneath", "library/std/fs/source/rename_beneath.c", "stdlib-operation"),
    ("r_std_fs_seek", "library/std/fs/source/seek.c", "stdlib-operation"),
    ("r_std_fs_sync", "library/std/fs/source/sync.c", "stdlib-operation"),
    ("r_std_fs_read_at", "library/std/fs/source/read_at.c", "stdlib-operation"),
    ("r_std_fs_write_all_at", "library/std/fs/source/write_all_at.c", "stdlib-operation"),
    ("r_std_fs_read_at_into", "library/std/fs/source/read_at_into.c", "stdlib-operation"),
    ("r_std_fs_write_all_at_from", "library/std/fs/source/write_all_at_from.c", "stdlib-operation"),
    ("r_std_fs_try_lock", "library/std/fs/source/try_lock.c", "stdlib-operation"),
    ("r_std_fs_lock", "library/std/fs/source/lock.c", "stdlib-operation"),
    ("r_std_fs_unlock", "library/std/fs/source/unlock.c", "stdlib-operation"),
    ("r_std_fs_write", "library/std/fs/source/write.c", "stdlib-operation"),
    ("r_std_fs_write_all", "library/std/fs/source/write_all.c", "stdlib-operation"),
    ("r_std_fs_write_file_atomic_no_replace", "library/std/fs/source/write_file_atomic_no_replace.c", "stdlib-operation"),
    ("r_std_fs_read_file", "library/std/fs/source/read_file.c", "stdlib-operation"),
    (
        "r_std_fs_write_file_atomic_no_replace_beneath",
        "library/std/fs/source/write_file_atomic_no_replace_beneath.c",
        "stdlib-operation",
    ),
    ("r_std_io_as_error", "library/std/io/source/as_error.c", "stdlib-operation"),
    ("r_std_io_close_input", "library/std/io/source/close_input.c", "stdlib-operation"),
    ("r_std_io_close_output", "library/std/io/source/close_output.c", "stdlib-operation"),
    ("r_std_io_flush", "library/std/io/source/flush.c", "stdlib-operation"),
    ("r_std_io_read", "library/std/io/source/read.c", "stdlib-operation"),
    ("r_std_io_stdin", "library/std/io/source/stdin.c", "stdlib-operation"),
    ("r_std_io_stderr", "library/std/io/source/stderr.c", "stdlib-operation"),
    ("r_std_io_stdout", "library/std/io/source/stdout.c", "stdlib-operation"),
    ("r_std_io_write", "library/std/io/source/write.c", "stdlib-operation"),
    ("r_std_io_write_all", "library/std/io/source/write_all.c", "stdlib-operation"),
    ("r_std_io_write_shared", "library/std/io/source/write_shared.c", "stdlib-operation"),
    (
        "r_std_process_clear_environment",
        "library/std/process/source/clear_environment.c",
        "stdlib-operation",
    ),
    ("r_std_rc_clone", "library/std/rc/source/clone.c", "stdlib-operation"),
    (
        "r_std_sync_call_once",
        "library/std/sync/source/call_once.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_call_once_force",
        "library/std/sync/source/call_once_force.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_get_or_init",
        "library/std/sync/source/get_or_init.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_once_lock",
        "library/std/sync/source/once_lock.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_once_new",
        "library/std/sync/source/once_new.c",
        "stdlib-operation",
    ),
    ("r_std_thread_detach", "library/std/thread/source/detach.c", "stdlib-operation"),
    ("r_std_thread_join", "library/std/thread/source/join.c", "stdlib-operation"),
    ("r_std_format_append_str", "library/std/format/source/append_str.c", "stdlib-operation"),
    ("r_std_format_append_char", "library/std/format/source/append_char.c", "stdlib-operation"),
    ("r_std_format_create", "library/std/format/source/create.c", "stdlib-operation"),
    ("r_std_format_finish", "library/std/format/source/finish.c", "stdlib-operation"),
    ("r_std_bytes_compare", "library/std/bytes/source/compare.c", "stdlib-operation"),
    ("r_std_bytes_copy", "library/std/bytes/source/copy.c", "stdlib-operation"),
    ("r_std_bytes_copy_within", "library/std/bytes/source/copy_within.c", "stdlib-operation"),
    ("r_std_bytes_ends_with", "library/std/bytes/source/ends_with.c", "stdlib-operation"),
    ("r_std_bytes_fill", "library/std/bytes/source/fill.c", "stdlib-operation"),
    ("r_std_bytes_find", "library/std/bytes/source/find.c", "stdlib-operation"),
    ("r_std_bytes_find_slice", "library/std/bytes/source/find_slice.c", "stdlib-operation"),
    ("r_std_bytes_starts_with", "library/std/bytes/source/starts_with.c", "stdlib-operation"),
    ("r_std_math_abs_f64", "library/std/math/source/abs_f64.c", "stdlib-operation"),
    ("r_std_math_sin_f64", "library/std/math/source/sin_f64.c", "stdlib-operation"),
    ("r_std_string_append_str", "library/std/string/source/append_str.c", "stdlib-operation"),
    ("r_std_string_append_utf8", "library/std/string/source/append_utf8.c", "stdlib-operation"),
    ("r_std_string_as_str", "library/std/string/source/as_str.c", "stdlib-operation"),
    ("r_std_string_clear", "library/std/string/source/clear.c", "stdlib-operation"),
    ("r_std_string_create", "library/std/string/source/create.c", "stdlib-operation"),
    ("r_std_string_from_str", "library/std/string/source/from_str.c", "stdlib-operation"),
    ("r_std_string_from_utf8", "library/std/string/source/from_utf8.c", "stdlib-operation"),
    ("r_std_string_into_bytes", "library/std/string/source/into_bytes.c", "stdlib-operation"),
    ("r_std_string_push_scalar", "library/std/string/source/push_scalar.c", "stdlib-operation"),
    ("r_std_string_reserve", "library/std/string/source/reserve.c", "stdlib-operation"),
    ("r_std_string_truncate", "library/std/string/source/truncate.c", "stdlib-operation"),
    ("r_std_string_with_capacity", "library/std/string/source/with_capacity.c", "stdlib-operation"),
    ("r_std_time_as_error", "library/std/time/source/as_error.c", "stdlib-operation"),
    ("r_std_time_duration_add", "library/std/time/source/duration_add.c", "stdlib-operation"),
    ("r_std_time_duration_compare", "library/std/time/source/duration_compare.c", "stdlib-operation"),
    ("r_std_time_duration_from_parts", "library/std/time/source/duration_from_parts.c", "stdlib-operation"),
    ("r_std_time_duration_multiply", "library/std/time/source/duration_multiply.c", "stdlib-operation"),
    ("r_std_time_duration_nanoseconds", "library/std/time/source/duration_nanoseconds.c", "stdlib-operation"),
    ("r_std_time_duration_seconds", "library/std/time/source/duration_seconds.c", "stdlib-operation"),
    ("r_std_time_duration_sub", "library/std/time/source/duration_sub.c", "stdlib-operation"),
    ("r_std_time_from_utc", "library/std/time/source/from_utc.c", "stdlib-operation"),
    ("r_std_time_instant_add", "library/std/time/source/instant_add.c", "stdlib-operation"),
    ("r_std_time_instant_duration", "library/std/time/source/instant_duration.c", "stdlib-operation"),
    ("r_std_time_system_add", "library/std/time/source/system_add.c", "stdlib-operation"),
    ("r_std_time_system_now", "library/std/time/source/system_now.c", "stdlib-operation"),
    ("r_std_time_to_utc", "library/std/time/source/to_utc.c", "stdlib-operation"),
    ("r_library_internal_format_integer", "library/internal/text/source/format_spec.c", "stdlib-operation"),
    ("r_library_internal_format_float", "library/internal/text/source/format_spec.c", "stdlib-operation"),
    ("r_library_internal_format_float32", "library/internal/text/source/format_spec.c", "stdlib-operation"),
    ("r_library_internal_format_text", "library/internal/text/source/format_spec.c", "stdlib-operation"),
    ("r_library_internal_format_char", "library/internal/text/source/format_spec.c", "stdlib-operation"),
    ("r_runtime_string_destroy", "runtime/source/string.c", "type-glue-direct"),
    # External project symbols reached by the header-static type-glue helpers below.
    # Reached directly by the generated per-element-type list helpers (node allocation and
    # release) and by the header type-glue helpers.
    ("r_runtime_allocator_deallocate", "runtime/source/allocator.c", "runtime-allocator"),
    ("r_runtime_allocator_allocate", "runtime/source/allocator.c", "runtime-allocator"),
    # Core R-STMT-0020: whether a budget refused the last allocation or start.
    ("r_runtime_allocation_refused_by_budget", "runtime/source/allocator.c", "runtime-allocator"),
    (
        "r_runtime_darwin_io_handle_release",
        "runtime/darwin/source/io_handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_directory_drop",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_directory_move",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_directory_entry_drop",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_directory_entry_move",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_directory_iter_drop",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_directory_iter_move",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_directory_next_result_drop",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_directory_next_result_move",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_file_drop",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_file_move",
        "library/internal/filesystem/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_write_file_result_drop",
        "library/internal/filesystem/source/async_atomic_write.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_fs_write_file_result_move",
        "library/internal/filesystem/source/async_atomic_write.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_io_read_result_drop",
        "library/internal/io/source/async_stream.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_io_read_result_move",
        "library/internal/io/source/async_stream.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_io_shared_write_result_drop",
        "library/internal/io/source/async_stream.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_io_shared_write_result_move",
        "library/internal/io/source/async_stream.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_io_write_result_drop",
        "library/internal/io/source/async_stream.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_io_write_result_move",
        "library/internal/io/source/async_stream.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_io_write_all_result_drop",
        "library/internal/io/source/async_stream.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_io_write_all_result_move",
        "library/internal/io/source/async_stream.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_thread_handle_destroy",
        "library/internal/thread/source/thread.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_thread_join_result_move",
        "library/internal/thread/source/thread.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_thread_spawn_checked",
        "library/internal/thread/source/thread.c",
        "compiler-runtime-boundary",
    ),
    (
        "r_library_internal_thread_spawn_scoped_checked",
        "library/internal/thread/source/thread.c",
        "compiler-runtime-boundary",
    ),
    (
        "r_library_internal_sync_once_destroy",
        "library/internal/synchronization/source/once.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_once_move",
        "library/internal/synchronization/source/once.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_once_lock_destroy",
        "library/internal/synchronization/source/once_lock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_once_lock_move",
        "library/internal/synchronization/source/once_lock.c",
        "type-glue-direct",
    ),
)

EXTERNAL_ENTRIES += (
    ("r_std_json_get", "library/std/json/source/get.c", "stdlib-operation"),
    ("r_std_json_find", "library/std/json/source/find.c", "stdlib-operation"),
    ("r_std_json_key_at", "library/std/json/source/key_at.c", "stdlib-operation"),
    ("r_std_json_take_index", "library/std/json/source/take_index.c", "stdlib-operation"),
    ("r_std_json_take_field", "library/std/json/source/take_field.c", "stdlib-operation"),
    ("r_std_json_array", "library/std/json/source/array.c", "stdlib-operation"),
    ("r_std_json_boolean", "library/std/json/source/boolean.c", "stdlib-operation"),
    ("r_std_json_from_bool", "library/std/json/source/from_bool.c", "stdlib-operation"),
    ("r_std_json_from_number", "library/std/json/source/from_number.c", "stdlib-operation"),
    ("r_std_json_from_string", "library/std/json/source/from_string.c", "stdlib-operation"),
    ("r_std_json_kind", "library/std/json/source/kind.c", "stdlib-operation"),
    ("r_std_json_len", "library/std/json/source/len.c", "stdlib-operation"),
    ("r_std_json_null", "library/std/json/source/null.c", "stdlib-operation"),
    ("r_std_json_number_is_zero", "library/std/json/source/number_is_zero.c", "stdlib-operation"),
    ("r_std_json_number_text", "library/std/json/source/number_text.c", "stdlib-operation"),
    ("r_std_json_object", "library/std/json/source/object.c", "stdlib-operation"),
    ("r_std_json_parse", "library/std/json/source/parse.c", "stdlib-operation"),
    ("r_std_json_parse_number", "library/std/json/source/parse_number.c", "stdlib-operation"),
    ("r_std_json_stringify", "library/std/json/source/stringify.c", "stdlib-operation"),
    ("r_std_json_text", "library/std/json/source/text.c", "stdlib-operation"),
    ("r_json_value_destroy", "library/internal/json/source/tree.c", "type-glue-direct"),
    ("r_json_number_destroy", "library/internal/json/source/common.c", "type-glue-direct"),
    ("r_json_error_destroy", "library/internal/json/source/common.c", "type-glue-direct"),
)

EXTERNAL_ENTRIES += (
    ("r_json_decode_skip_create", "library/internal/json/source/decoder.c", "stdlib-operation"),
    ("r_json_scanner_take_text", "library/internal/json/source/scanner.c", "stdlib-operation"),
    ("r_json_decode_push", "library/internal/json/source/decoder.c", "stdlib-operation"),
    ("r_json_decode_value_create", "library/internal/json/source/decoder.c", "stdlib-operation"),
    ("r_json_decoder_initialize", "library/internal/json/source/decoder.c", "stdlib-operation"),
    ("r_json_decoder_feed", "library/internal/json/source/decoder.c", "stdlib-operation"),
    ("r_json_decoder_take", "library/internal/json/source/decoder.c", "stdlib-operation"),
    ("r_json_decoder_destroy", "library/internal/json/source/decoder.c", "type-glue-direct"),
    ("r_json_cursor_boolean", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_char", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_default_string", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_expect", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_fail", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_finish", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_float", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_long_double", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_initialize", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_integer", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_next", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_skip", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_cursor_string", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_std_json_name_equal", "library/std/json/source/name_equal.c", "stdlib-operation"),
)

EXTERNAL_ENTRIES += (
    ("r_std_json_parse_with_options", "library/std/json/source/parse_with_options.c", "stdlib-operation"),
    ("r_std_json_stringify_with_options", "library/std/json/source/stringify_with_options.c", "stdlib-operation"),
)

EXTERNAL_ENTRIES += (
    ("r_json_encode_integer", "library/internal/json/source/encode.c", "stdlib-operation"),
    ("r_json_encode_float", "library/internal/json/source/encode.c", "stdlib-operation"),
    ("r_json_encode_char", "library/internal/json/source/encode.c", "stdlib-operation"),
    ("r_json_value_empty", "library/internal/json/source/encode.c", "stdlib-operation"),
    ("r_std_json_insert", "library/std/json/source/insert.c", "stdlib-operation"),
    ("r_std_json_append", "library/std/json/source/append.c", "stdlib-operation"),
)

EXTERNAL_ENTRIES += (
    ("r_json_cursor_array_push", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_object_extend", "library/internal/json/source/tree.c", "stdlib-operation"),
    ("r_json_cursor_value", "library/internal/json/source/tree.c", "stdlib-operation"),
    ("r_json_clone_value", "library/internal/json/source/tree.c", "stdlib-operation"),
    ("r_json_cursor_number", "library/internal/json/source/cursor.c", "stdlib-operation"),
    ("r_json_quote_number", "library/internal/json/source/encode.c", "stdlib-operation"),
)

EXTERNAL_ENTRIES += (
    ("r_json_reader_initialize", "library/internal/json/source/reader.c", "stdlib-operation"),
    ("r_json_reader_read_next", "library/internal/json/source/reader.c", "stdlib-operation"),
    ("r_json_reader_detach", "library/internal/json/source/reader.c", "stdlib-operation"),
    ("r_json_detached_take_handle", "library/internal/json/source/reader.c", "stdlib-operation"),
    ("r_json_detached_take_bytes", "library/internal/json/source/reader.c", "stdlib-operation"),
    ("r_json_reader_destroy", "library/internal/json/source/reader.c", "type-glue-direct"),
    ("r_json_detached_destroy", "library/internal/json/source/reader.c", "type-glue-direct"),
    ("r_std_fs_read", "library/std/fs/source/read.c", "stdlib-operation"),
    ("r_std_net_tcp_read", "library/std/net/source/tcp_read.c", "stdlib-operation"),
    ("r_std_io_read_into", "library/std/io/source/read_into.c", "stdlib-operation"),
    ("r_std_io_write_from", "library/std/io/source/write_from.c", "stdlib-operation"),
    ("r_std_io_write_all_from", "library/std/io/source/write_all_from.c", "stdlib-operation"),
    ("r_std_fs_read_into", "library/std/fs/source/read_into.c", "stdlib-operation"),
    ("r_std_fs_write_from", "library/std/fs/source/write_from.c", "stdlib-operation"),
    ("r_std_fs_write_all_from", "library/std/fs/source/write_all_from.c", "stdlib-operation"),
    ("r_std_net_tcp_read_into", "library/std/net/source/tcp_read_into.c", "stdlib-operation"),
    ("r_std_net_tcp_write_from", "library/std/net/source/tcp_write_from.c", "stdlib-operation"),
    (
        "r_std_net_tcp_write_all_from",
        "library/std/net/source/tcp_write_all_from.c",
        "stdlib-operation",
    ),
    ("r_std_net_udp_send_from", "library/std/net/source/udp_send_from.c", "stdlib-operation"),
    (
        "r_std_net_udp_receive_into",
        "library/std/net/source/udp_receive_into.c",
        "stdlib-operation",
    ),
    (
        "r_std_net_tcp_local_address",
        "library/std/net/source/tcp_local_address.c",
        "stdlib-operation",
    ),
    (
        "r_std_time_duration_from_seconds",
        "library/std/time/source/duration_from_seconds.c",
        "stdlib-operation",
    ),
    ("r_std_time_monotonic_now", "library/std/time/source/monotonic_now.c", "stdlib-operation"),
)

EXTERNAL_ENTRIES += (
    ("r_library_internal_net_tcp_stream_move", "library/internal/networking/source/handle.c", "type-glue-direct"),
    ("r_library_internal_net_tcp_stream_drop", "library/internal/networking/source/handle.c", "type-glue-direct"),
)

EXTERNAL_ENTRIES += (
    ("r_library_internal_net_unix_listener_move", "library/internal/networking/source/unix.c", "type-glue-direct"),
    ("r_library_internal_net_unix_listener_drop", "library/internal/networking/source/unix.c", "type-glue-direct"),
    ("r_library_internal_net_unix_stream_move", "library/internal/networking/source/unix.c", "type-glue-direct"),
    ("r_library_internal_net_unix_stream_drop", "library/internal/networking/source/unix.c", "type-glue-direct"),
    ("r_library_internal_net_unix_datagram_move", "library/internal/networking/source/unix.c", "type-glue-direct"),
    ("r_library_internal_net_unix_datagram_drop", "library/internal/networking/source/unix.c", "type-glue-direct"),
)

EXTERNAL_ENTRIES += (
    ("r_std_convert_parse_c_double", "library/std/convert/source/parse_c_double.c", "stdlib-operation"),
    ("r_std_convert_parse_c_float", "library/std/convert/source/parse_c_float.c", "stdlib-operation"),
    ("r_std_convert_parse_c_long_double", "library/std/convert/source/parse_c_long_double.c", "stdlib-operation"),
    ("r_std_convert_parse_f32", "library/std/convert/source/parse_f32.c", "stdlib-operation"),
    ("r_std_convert_parse_f64", "library/std/convert/source/parse_f64.c", "stdlib-operation"),
    ("r_std_convert_parse_suffix", "library/std/convert/source/parse_suffix.c", "stdlib-operation"),
    ("r_std_env_arguments", "library/std/env/source/arguments.c", "stdlib-operation"),
    ("r_std_env_get", "library/std/env/source/get.c", "stdlib-operation"),
    ("r_std_env_remove", "library/std/env/source/remove.c", "stdlib-operation"),
    ("r_std_env_set", "library/std/env/source/set.c", "stdlib-operation"),
    ("r_std_env_variables", "library/std/env/source/variables.c", "stdlib-operation"),
    ("r_std_error_diagnostic", "library/std/error/source/diagnostic.c", "stdlib-operation"),
    ("r_std_error_name", "library/std/error/source/name.c", "stdlib-operation"),
    ("r_std_format_append_c_double", "library/std/format/source/append_c_double.c", "stdlib-operation"),
    ("r_std_format_append_c_float", "library/std/format/source/append_c_float.c", "stdlib-operation"),
    ("r_std_format_append_c_long_double", "library/std/format/source/append_c_long_double.c", "stdlib-operation"),
    ("r_std_format_append_f32", "library/std/format/source/append_f32.c", "stdlib-operation"),
    ("r_std_format_append_f64", "library/std/format/source/append_f64.c", "stdlib-operation"),
    ("r_std_format_append_suffix", "library/std/format/source/append_suffix.c", "stdlib-operation"),
    ("r_std_format_as_str", "library/std/format/source/as_str.c", "stdlib-operation"),
    ("r_std_format_clear", "library/std/format/source/clear.c", "stdlib-operation"),
    ("r_std_format_with_capacity", "library/std/format/source/with_capacity.c", "stdlib-operation"),
    ("r_std_fs_path_clone", "library/std/fs/source/path_clone.c", "stdlib-operation"),
    ("r_std_fs_path_join", "library/std/fs/source/path_join.c", "stdlib-operation"),
    ("r_std_fs_path_to_utf8", "library/std/fs/source/path_to_utf8.c", "stdlib-operation"),
    ("r_std_net_format_ip", "library/std/net/source/format_ip.c", "stdlib-operation"),
    ("r_library_internal_net_ip_text", "library/std/net/source/format_ip.c", "stdlib-operation"),
    ("r_library_internal_net_socket_text", "library/std/net/source/format_ip.c", "stdlib-operation"),
    ("r_std_net_parse_ip", "library/std/net/source/parse_ip.c", "stdlib-operation"),
    ("r_std_string_from_bytes", "library/std/string/source/from_bytes.c", "stdlib-operation"),
    ("r_std_sync_barrier_new", "library/std/sync/source/barrier_new.c", "stdlib-operation"),
    ("r_std_sync_barrier_wait", "library/std/sync/source/barrier_wait.c", "stdlib-operation"),
    ("r_std_sync_condvar_new", "library/std/sync/source/condvar_new.c", "stdlib-operation"),
    ("r_std_sync_notify_all", "library/std/sync/source/notify_all.c", "stdlib-operation"),
    ("r_std_sync_notify_one", "library/std/sync/source/notify_one.c", "stdlib-operation"),
    ("r_std_thread_clone_thread", "library/std/thread/source/clone_thread.c", "stdlib-operation"),
    ("r_std_thread_current", "library/std/thread/source/current.c", "stdlib-operation"),
    ("r_std_thread_panic_category", "library/std/thread/source/panic_category.c", "stdlib-operation"),
    ("r_std_thread_panic_text", "library/std/thread/source/panic_text.c", "stdlib-operation"),
    ("r_std_thread_park", "library/std/thread/source/park.c", "stdlib-operation"),
    ("r_std_thread_sleep_nanoseconds", "library/std/thread/source/sleep_nanoseconds.c", "stdlib-operation"),
    ("r_std_thread_unpark", "library/std/thread/source/unpark.c", "stdlib-operation"),
    ("r_std_thread_yield_now", "library/std/thread/source/yield_now.c", "stdlib-operation"),
    ("r_std_time_sleep_for", "library/std/time/source/sleep_for.c", "stdlib-operation"),
    ("r_std_time_sleep_until", "library/std/time/source/sleep_until.c", "stdlib-operation"),
    ("r_library_internal_sync_barrier_destroy", "library/internal/synchronization/source/barrier.c", "type-glue-direct"),
    ("r_library_internal_sync_barrier_move", "library/internal/synchronization/source/barrier.c", "type-glue-direct"),
    ("r_library_internal_sync_condvar_destroy", "library/internal/synchronization/source/condvar.c", "type-glue-direct"),
    ("r_library_internal_sync_condvar_move", "library/internal/synchronization/source/condvar.c", "type-glue-direct"),
    ("r_library_internal_thread_identity_destroy", "library/internal/thread/source/thread.c", "type-glue-direct"),
    ("r_library_internal_thread_join_result_destroy", "library/internal/thread/source/thread.c", "type-glue-direct"),
    ("r_library_internal_thread_panic_report_destroy", "library/internal/thread/source/thread.c", "type-glue-direct"),
)

EXTERNAL_ENTRIES += (
    (
        "r_runtime_thread_local_cleanup_install",
        "runtime/source/thread_attachment.c",
        "runtime-hosted",
    ),
    ("r_std_arc_clone_weak", "library/std/arc/source/clone_weak.c", "stdlib-operation"),
    ("r_std_arc_downgrade", "library/std/arc/source/downgrade.c", "stdlib-operation"),
    ("r_std_arc_from_raw", "library/std/arc/source/from_raw.c", "stdlib-operation"),
    ("r_std_arc_get_mut", "library/std/arc/source/get_mut.c", "stdlib-operation"),
    ("r_std_arc_into_raw", "library/std/arc/source/into_raw.c", "stdlib-operation"),
    ("r_std_arc_ptr_eq", "library/std/arc/source/ptr_eq.c", "stdlib-operation"),
    ("r_std_arc_strong_count", "library/std/arc/source/strong_count.c", "stdlib-operation"),
    ("r_std_arc_try_unwrap", "library/std/arc/source/try_unwrap.c", "stdlib-operation"),
    ("r_std_arc_upgrade", "library/std/arc/source/upgrade.c", "stdlib-operation"),
    ("r_std_arc_weak_count", "library/std/arc/source/weak_count.c", "stdlib-operation"),
    ("r_std_rc_clone_weak", "library/std/rc/source/clone_weak.c", "stdlib-operation"),
    ("r_std_rc_downgrade", "library/std/rc/source/downgrade.c", "stdlib-operation"),
    ("r_std_rc_from_raw", "library/std/rc/source/from_raw.c", "stdlib-operation"),
    ("r_std_rc_get_mut", "library/std/rc/source/get_mut.c", "stdlib-operation"),
    ("r_std_rc_into_raw", "library/std/rc/source/into_raw.c", "stdlib-operation"),
    ("r_std_rc_ptr_eq", "library/std/rc/source/ptr_eq.c", "stdlib-operation"),
    ("r_std_rc_strong_count", "library/std/rc/source/strong_count.c", "stdlib-operation"),
    ("r_std_rc_try_unwrap", "library/std/rc/source/try_unwrap.c", "stdlib-operation"),
    ("r_std_rc_upgrade", "library/std/rc/source/upgrade.c", "stdlib-operation"),
    ("r_std_rc_weak_count", "library/std/rc/source/weak_count.c", "stdlib-operation"),
    ("r_std_net_resolve", "library/std/net/source/resolve.c", "stdlib-operation"),
    ("r_std_net_tcp_accept", "library/std/net/source/tcp_accept.c", "stdlib-operation"),
    ("r_std_net_tcp_close", "library/std/net/source/tcp_close.c", "stdlib-operation"),
    ("r_std_net_tcp_connect", "library/std/net/source/tcp_connect.c", "stdlib-operation"),
    ("r_std_net_tcp_listen", "library/std/net/source/tcp_listen.c", "stdlib-operation"),
    (
        "r_std_net_tcp_listener_close",
        "library/std/net/source/tcp_listener_close.c",
        "stdlib-operation",
    ),
    (
        "r_std_net_tcp_listener_local_address",
        "library/std/net/source/tcp_listener_local_address.c",
        "stdlib-operation",
    ),
    (
        "r_std_net_tcp_peer_address",
        "library/std/net/source/tcp_peer_address.c",
        "stdlib-operation",
    ),
    ("r_std_net_tcp_shutdown", "library/std/net/source/tcp_shutdown.c", "stdlib-operation"),
    ("r_std_net_tcp_write", "library/std/net/source/tcp_write.c", "stdlib-operation"),
    (
        "r_std_net_tcp_write_all",
        "library/std/net/source/tcp_write_all.c",
        "stdlib-operation",
    ),
    ("r_std_net_udp_bind", "library/std/net/source/udp_bind.c", "stdlib-operation"),
    ("r_std_net_udp_close", "library/std/net/source/udp_close.c", "stdlib-operation"),
    (
        "r_std_net_udp_local_address",
        "library/std/net/source/udp_local_address.c",
        "stdlib-operation",
    ),
    (
        "r_std_net_udp_receive_from",
        "library/std/net/source/udp_receive_from.c",
        "stdlib-operation",
    ),
    ("r_std_net_udp_send_to", "library/std/net/source/udp_send_to.c", "stdlib-operation"),
    ("r_std_net_tcp_get_options", "library/std/net/source/tcp_get_options.c", "stdlib-operation"),
    ("r_std_net_tcp_set_options", "library/std/net/source/tcp_set_options.c", "stdlib-operation"),
    ("r_std_net_udp_get_options", "library/std/net/source/udp_get_options.c", "stdlib-operation"),
    ("r_std_net_udp_set_options", "library/std/net/source/udp_set_options.c", "stdlib-operation"),
    ("r_std_net_udp_join_multicast", "library/std/net/source/udp_join_multicast.c", "stdlib-operation"),
    ("r_std_net_udp_leave_multicast", "library/std/net/source/udp_leave_multicast.c", "stdlib-operation"),
    ("r_std_net_unix_accept", "library/std/net/source/unix_accept.c", "stdlib-operation"),
    ("r_std_net_unix_close", "library/std/net/source/unix_close.c", "stdlib-operation"),
    ("r_std_net_unix_connect", "library/std/net/source/unix_connect.c", "stdlib-operation"),
    ("r_std_net_unix_datagram_bind", "library/std/net/source/unix_datagram_bind.c", "stdlib-operation"),
    ("r_std_net_unix_datagram_close", "library/std/net/source/unix_datagram_close.c", "stdlib-operation"),
    ("r_std_net_unix_datagram_connect", "library/std/net/source/unix_datagram_connect.c", "stdlib-operation"),
    ("r_std_net_unix_listen", "library/std/net/source/unix_listen.c", "stdlib-operation"),
    ("r_std_net_unix_listener_close", "library/std/net/source/unix_listener_close.c", "stdlib-operation"),
    ("r_std_net_unix_peer_credentials", "library/std/net/source/unix_peer_credentials.c", "stdlib-operation"),
    ("r_std_net_unix_read_into", "library/std/net/source/unix_read_into.c", "stdlib-operation"),
    ("r_std_net_unix_receive_into", "library/std/net/source/unix_receive_into.c", "stdlib-operation"),
    ("r_std_net_unix_send_from", "library/std/net/source/unix_send_from.c", "stdlib-operation"),
    ("r_std_net_unix_shutdown", "library/std/net/source/unix_shutdown.c", "stdlib-operation"),
    ("r_std_net_unix_write_all_from", "library/std/net/source/unix_write_all_from.c", "stdlib-operation"),
    ("r_std_net_unix_write_from", "library/std/net/source/unix_write_from.c", "stdlib-operation"),
    ("r_std_process_abort", "library/std/process/source/abort.c", "stdlib-operation"),
    ("r_std_process_arg", "library/std/process/source/arg.c", "stdlib-operation"),
    (
        "r_std_process_command_create",
        "library/std/process/source/command_create.c",
        "stdlib-operation",
    ),
    (
        "r_std_process_environment",
        "library/std/process/source/environment.c",
        "stdlib-operation",
    ),
    ("r_std_process_exit", "library/std/process/source/exit.c", "stdlib-operation"),
    ("r_std_process_id", "library/std/process/source/id.c", "stdlib-operation"),
    (
        "r_std_process_remove_environment",
        "library/std/process/source/remove_environment.c",
        "stdlib-operation",
    ),
    (
        "r_std_process_set_stdio",
        "library/std/process/source/set_stdio.c",
        "stdlib-operation",
    ),
    ("r_std_process_spawn", "library/std/process/source/spawn.c", "stdlib-operation"),
    (
        "r_std_process_take_stderr",
        "library/std/process/source/take_stderr.c",
        "stdlib-operation",
    ),
    (
        "r_std_process_take_stdin",
        "library/std/process/source/take_stdin.c",
        "stdlib-operation",
    ),
    (
        "r_std_process_take_stdout",
        "library/std/process/source/take_stdout.c",
        "stdlib-operation",
    ),
    ("r_std_process_terminate", "library/std/process/source/terminate.c", "stdlib-operation"),
    ("r_std_process_wait", "library/std/process/source/wait.c", "stdlib-operation"),
    (
        "r_std_process_working_directory",
        "library/std/process/source/working_directory.c",
        "stdlib-operation",
    ),
    ("r_std_signal_listen", "library/std/signal/source/listen.c", "stdlib-operation"),
    ("r_std_signal_next", "library/std/signal/source/next.c", "stdlib-operation"),
    ("r_std_signal_raise", "library/std/signal/source/raise.c", "stdlib-operation"),
    ("r_std_sync_channel", "library/std/sync/source/channel.c", "stdlib-operation"),
    (
        "r_std_sync_clone_sender",
        "library/std/sync/source/clone_sender.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_clone_sync_sender",
        "library/std/sync/source/clone_sync_sender.c",
        "stdlib-operation",
    ),
    ("r_std_sync_get", "library/std/sync/source/get.c", "stdlib-operation"),
    ("r_std_sync_lock", "library/std/sync/source/lock.c", "stdlib-operation"),
    (
        "r_std_sync_mutex_guard_mut",
        "library/std/sync/source/mutex_guard_mut.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_mutex_guard_ref",
        "library/std/sync/source/mutex_guard_ref.c",
        "stdlib-operation",
    ),
    ("r_std_sync_mutex_new", "library/std/sync/source/mutex_new.c", "stdlib-operation"),
    ("r_std_sync_read", "library/std/sync/source/read.c", "stdlib-operation"),
    ("r_std_sync_receive", "library/std/sync/source/receive.c", "stdlib-operation"),
    ("r_std_sync_receiver", "library/std/sync/source/receiver.c", "stdlib-operation"),
    ("r_std_sync_recv", "library/std/sync/source/recv.c", "stdlib-operation"),
    (
        "r_std_sync_rw_read_guard_ref",
        "library/std/sync/source/rw_read_guard_ref.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_rw_write_guard_mut",
        "library/std/sync/source/rw_write_guard_mut.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_rw_write_guard_ref",
        "library/std/sync/source/rw_write_guard_ref.c",
        "stdlib-operation",
    ),
    ("r_std_sync_rwlock_new", "library/std/sync/source/rwlock_new.c", "stdlib-operation"),
    ("r_std_sync_send", "library/std/sync/source/send.c", "stdlib-operation"),
    ("r_std_sync_sender", "library/std/sync/source/sender.c", "stdlib-operation"),
    ("r_std_sync_set", "library/std/sync/source/set.c", "stdlib-operation"),
    (
        "r_std_sync_sync_channel",
        "library/std/sync/source/sync_channel.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_sync_receiver",
        "library/std/sync/source/sync_receiver.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_sync_send",
        "library/std/sync/source/sync_send.c",
        "stdlib-operation",
    ),
    (
        "r_std_sync_sync_sender",
        "library/std/sync/source/sync_sender.c",
        "stdlib-operation",
    ),
    ("r_std_sync_try_lock", "library/std/sync/source/try_lock.c", "stdlib-operation"),
    ("r_std_sync_try_read", "library/std/sync/source/try_read.c", "stdlib-operation"),
    ("r_std_sync_try_recv", "library/std/sync/source/try_recv.c", "stdlib-operation"),
    ("r_std_sync_try_send", "library/std/sync/source/try_send.c", "stdlib-operation"),
    ("r_std_sync_try_write", "library/std/sync/source/try_write.c", "stdlib-operation"),
    ("r_std_sync_unlock", "library/std/sync/source/unlock.c", "stdlib-operation"),
    ("r_std_sync_wait", "library/std/sync/source/wait.c", "stdlib-operation"),
    ("r_std_sync_write", "library/std/sync/source/write.c", "stdlib-operation"),
    (
        "r_library_internal_sync_mutex_move",
        "library/internal/synchronization/source/mutex.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_mutex_guard_move",
        "library/internal/synchronization/source/mutex.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_rw_lock_move",
        "library/internal/synchronization/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_rw_read_guard_move",
        "library/internal/synchronization/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_rw_write_guard_move",
        "library/internal/synchronization/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_net_tcp_listener_drop",
        "library/internal/networking/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_net_tcp_listener_move",
        "library/internal/networking/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_net_udp_socket_drop",
        "library/internal/networking/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_net_udp_socket_move",
        "library/internal/networking/source/handle.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_process_child_destroy",
        "library/internal/process/source/child.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_signal_listener_destroy",
        "library/internal/signal/source/listener.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_signal_listener_move",
        "library/internal/signal/source/listener.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_process_child_move",
        "library/internal/process/source/child.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_process_command_destroy",
        "library/internal/process/source/command.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_process_command_move",
        "library/internal/process/source/command.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_channel_destroy",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_channel_move",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_mutex_destroy",
        "library/internal/synchronization/source/mutex.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_mutex_guard_destroy",
        "library/internal/synchronization/source/mutex.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_receiver_destroy",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_receiver_move",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_rw_lock_destroy",
        "library/internal/synchronization/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_rw_read_guard_destroy",
        "library/internal/synchronization/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_rw_write_guard_destroy",
        "library/internal/synchronization/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_sender_destroy",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_sender_move",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_sync_channel_destroy",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_sync_channel_move",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_sync_sender_destroy",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_sync_sender_move",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
)

# L30 (R-SLIB-ASYNC-0013..0016, R-LIB-0016): std.async synchronization and bounded-channel reservations.
EXTERNAL_ENTRIES += (
    ("r_std_async_blocking", "library/std/async/source/blocking.c", "stdlib-operation"),
    ("r_std_async_acquire", "library/std/async/source/acquire.c", "stdlib-operation"),
    ("r_std_async_add_permits", "library/std/async/source/add_permits.c", "stdlib-operation"),
    (
        "r_std_async_available_permits",
        "library/std/async/source/available_permits.c",
        "stdlib-operation",
    ),
    ("r_std_async_broadcast", "library/std/async/source/broadcast.c", "stdlib-operation"),
    (
        "r_std_async_broadcast_receive",
        "library/std/async/source/broadcast_receive.c",
        "stdlib-operation",
    ),
    (
        "r_std_async_clone_broadcast",
        "library/std/async/source/clone_broadcast.c",
        "stdlib-operation",
    ),
    ("r_std_async_clone_mutex", "library/std/async/source/clone_mutex.c", "stdlib-operation"),
    ("r_std_async_clone_notify", "library/std/async/source/clone_notify.c", "stdlib-operation"),
    ("r_std_async_clone_rw_lock", "library/std/async/source/clone_rw_lock.c", "stdlib-operation"),
    (
        "r_std_async_clone_semaphore",
        "library/std/async/source/clone_semaphore.c",
        "stdlib-operation",
    ),
    ("r_std_async_lock", "library/std/async/source/lock.c", "stdlib-operation"),
    (
        "r_std_async_mutex_guard_mut",
        "library/std/async/source/mutex_guard_mut.c",
        "stdlib-operation",
    ),
    (
        "r_std_async_mutex_guard_ref",
        "library/std/async/source/mutex_guard_ref.c",
        "stdlib-operation",
    ),
    ("r_std_async_mutex_new", "library/std/async/source/mutex_new.c", "stdlib-operation"),
    ("r_std_async_notified", "library/std/async/source/notified.c", "stdlib-operation"),
    ("r_std_async_notify_all", "library/std/async/source/notify_all.c", "stdlib-operation"),
    ("r_std_async_notify_new", "library/std/async/source/notify_new.c", "stdlib-operation"),
    ("r_std_async_notify_one", "library/std/async/source/notify_one.c", "stdlib-operation"),
    ("r_std_async_publish", "library/std/async/source/publish.c", "stdlib-operation"),
    ("r_std_async_read", "library/std/async/source/read.c", "stdlib-operation"),
    ("r_std_async_release", "library/std/async/source/release.c", "stdlib-operation"),
    (
        "r_std_async_rw_read_guard_ref",
        "library/std/async/source/rw_read_guard_ref.c",
        "stdlib-operation",
    ),
    (
        "r_std_async_rw_write_guard_mut",
        "library/std/async/source/rw_write_guard_mut.c",
        "stdlib-operation",
    ),
    (
        "r_std_async_rw_write_guard_ref",
        "library/std/async/source/rw_write_guard_ref.c",
        "stdlib-operation",
    ),
    ("r_std_async_rwlock_new", "library/std/async/source/rwlock_new.c", "stdlib-operation"),
    ("r_std_async_semaphore_new", "library/std/async/source/semaphore_new.c", "stdlib-operation"),
    ("r_std_async_subscribe", "library/std/async/source/subscribe.c", "stdlib-operation"),
    ("r_std_async_try_acquire", "library/std/async/source/try_acquire.c", "stdlib-operation"),
    ("r_std_async_try_lock", "library/std/async/source/try_lock.c", "stdlib-operation"),
    ("r_std_async_try_read", "library/std/async/source/try_read.c", "stdlib-operation"),
    ("r_std_async_try_write", "library/std/async/source/try_write.c", "stdlib-operation"),
    ("r_std_async_unlock", "library/std/async/source/unlock.c", "stdlib-operation"),
    ("r_std_async_write", "library/std/async/source/write.c", "stdlib-operation"),
    ("r_std_sync_reserve", "library/std/sync/source/reserve.c", "stdlib-operation"),
    ("r_std_sync_try_reserve", "library/std/sync/source/try_reserve.c", "stdlib-operation"),
    ("r_std_sync_send_permit", "library/std/sync/source/send_permit.c", "stdlib-operation"),
    (
        "r_library_internal_async_mutex_release",
        "library/internal/async_sync/source/mutex.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_mutex_unlock",
        "library/internal/async_sync/source/mutex.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_rwlock_release",
        "library/internal/async_sync/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_rwlock_read_unlock",
        "library/internal/async_sync/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_rwlock_write_unlock",
        "library/internal/async_sync/source/rwlock.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_semaphore_release",
        "library/internal/async_sync/source/semaphore.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_semaphore_return",
        "library/internal/async_sync/source/semaphore.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_notify_release",
        "library/internal/async_sync/source/notify.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_broadcast_release",
        "library/internal/async_sync/source/broadcast.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_async_broadcast_unsubscribe",
        "library/internal/async_sync/source/broadcast.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_permit_destroy",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_sync_permit_move",
        "library/internal/synchronization/source/channel.c",
        "type-glue-direct",
    ),
)

# B6: further entries the LLVM emitter names by string literals. Sorted by symbol.
EXTERNAL_ENTRIES += (
    ("r_json_cursor_destroy", "library/internal/json/source/cursor.c", "type-glue-direct"),
    ("r_json_decoder_abort", "library/internal/json/source/decoder.c", "type-glue-direct"),
    ("r_json_decoder_rebind", "library/internal/json/source/decoder.c", "type-glue-direct"),
    ("r_json_scanner_destroy", "library/internal/json/source/scanner.c", "type-glue-direct"),
    ("r_json_scanner_failure", "library/internal/json/source/scanner.c", "type-glue-direct"),
    ("r_json_scanner_feed", "library/internal/json/source/scanner.c", "type-glue-direct"),
    ("r_json_scanner_initialize", "library/internal/json/source/scanner.c", "type-glue-direct"),
    ("r_json_tree_builder_destroy", "library/internal/json/source/tree.c", "type-glue-direct"),
    ("r_json_tree_builder_initialize", "library/internal/json/source/tree.c", "type-glue-direct"),
    ("r_json_tree_builder_token", "library/internal/json/source/tree.c", "type-glue-direct"),
    (
        "r_library_internal_thread_panic_report_from",
        "library/internal/thread/source/thread.c",
        "type-glue-direct",
    ),
    (
        "r_library_internal_thread_panic_report_take",
        "library/internal/thread/source/thread.c",
        "type-glue-direct",
    ),
    ("r_runtime_allocator_attempt_count", "runtime/source/allocator.c", "runtime-allocator"),
    ("r_runtime_allocator_initialize", "runtime/source/allocator.c", "runtime-allocator"),
    ("r_runtime_allocator_reallocate", "runtime/source/allocator.c", "runtime-allocator"),
    ("r_runtime_allocator_set_failure", "runtime/source/allocator.c", "runtime-allocator"),
    ("r_runtime_arc_destroy_begin", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_destroy_finish", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_destroy_scratch", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_destroy_value", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_downgrade", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_from_raw", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_get_mut", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_into_raw", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_ptr_eq", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_strong_count", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_try_unwrap", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_arc_weak_count", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_array_clear", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_destroy_count", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_destroy_element", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_destroy_finish", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_destroy_locate", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_destroy_scratch", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_get", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_get_mut", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_pop", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_push", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_remove", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_array_reserve", "runtime/source/array.c", "runtime-container"),
    ("r_runtime_blocking_reserve", "runtime/darwin/source/blocking_pool.inc", "runtime-task"),
    ("r_runtime_blocking_start", "runtime/darwin/source/blocking_pool.inc", "runtime-task"),
    ("r_runtime_blocking_stop", "runtime/darwin/source/blocking_pool.inc", "runtime-task"),
    ("r_runtime_blocking_submit", "runtime/darwin/source/blocking_pool.inc", "runtime-task"),
    ("r_runtime_blocking_unreserve", "runtime/darwin/source/blocking_pool.inc", "runtime-task"),
    ("r_runtime_blocking_withdraw", "runtime/darwin/source/blocking_pool.inc", "runtime-task"),
    ("r_runtime_budget_bytes_available", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_bytes_limit", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_bytes_used", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_charge_task", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_create", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_current", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_note_refusal", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_refusing", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_release", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_retain", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_return_task", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_swap_current", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_tasks_limit", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_budget_tasks_used", "runtime/source/allocator.c", "runtime-task"),
    ("r_runtime_darwin_io_buffer_allocate", "runtime/darwin/source/io_buffer.c", "runtime-io"),
    ("r_runtime_darwin_io_buffer_release", "runtime/darwin/source/io_buffer.c", "runtime-io"),
    ("r_runtime_darwin_io_handle_create", "runtime/darwin/source/io_handle.c", "runtime-io"),
    ("r_runtime_darwin_io_handle_create_file", "runtime/darwin/source/io_handle.c", "runtime-io"),
    (
        "r_runtime_darwin_io_handle_create_socket",
        "runtime/darwin/source/io_handle.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_handle_release_with_cleanup",
        "runtime/darwin/source/io_handle.c",
        "runtime-io",
    ),
    ("r_runtime_darwin_io_handle_retain_view", "runtime/darwin/source/io_handle.c", "runtime-io"),
    (
        "r_runtime_darwin_io_handle_terminal_close_failure",
        "runtime/darwin/source/io_handle.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_prepare_borrowed_random_shared_write",
        "runtime/darwin/source/io_write.c",
        "runtime-io",
    ),
    ("r_runtime_darwin_io_prepare_close", "runtime/darwin/source/io_close.c", "runtime-io"),
    ("r_runtime_darwin_io_prepare_flush", "runtime/darwin/source/io_flush.c", "runtime-io"),
    ("r_runtime_darwin_io_prepare_read", "runtime/darwin/source/io_read.c", "runtime-io"),
    ("r_runtime_darwin_io_prepare_read_some", "runtime/darwin/source/io_read.c", "runtime-io"),
    ("r_runtime_darwin_io_prepare_shared_write", "runtime/darwin/source/io_write.c", "runtime-io"),
    ("r_runtime_darwin_io_prepare_shutdown", "runtime/darwin/source/io_shutdown.c", "runtime-io"),
    ("r_runtime_darwin_io_prepare_write", "runtime/darwin/source/io_write.c", "runtime-io"),
    ("r_runtime_darwin_io_prepare_write_some", "runtime/darwin/source/io_write.c", "runtime-io"),
    ("r_runtime_darwin_io_prepared_abort", "runtime/darwin/source/io_prepare.c", "runtime-io"),
    ("r_runtime_darwin_io_prepared_activate", "runtime/darwin/source/io_prepare.c", "runtime-io"),
    (
        "r_runtime_darwin_io_prepared_activate_close",
        "runtime/darwin/source/io_close.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_prepared_bind_activate_borrowed_random_shared_write",
        "runtime/darwin/source/io_write.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_prepared_set_offset",
        "runtime/darwin/source/io_prepare.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_prepared_set_shutdown_entry",
        "runtime/darwin/source/io_shutdown.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_prepared_set_stream_position",
        "runtime/darwin/source/io_prepare.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_process_console_start",
        "runtime/darwin/source/io_console.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_process_console_stop",
        "runtime/darwin/source/io_console.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_process_stderr_retain",
        "runtime/darwin/source/io_console.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_process_stdin_retain",
        "runtime/darwin/source/io_console.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_process_stdout_retain",
        "runtime/darwin/source/io_console.c",
        "runtime-io",
    ),
    ("r_runtime_darwin_io_request_cancel", "runtime/darwin/source/io_request.c", "runtime-io"),
    (
        "r_runtime_darwin_io_request_deadline_expired",
        "runtime/darwin/source/io_request.c",
        "runtime-io",
    ),
    ("r_runtime_darwin_io_request_progress", "runtime/darwin/source/io_request.c", "runtime-io"),
    ("r_runtime_darwin_io_request_release", "runtime/darwin/source/io_request.c", "runtime-io"),
    (
        "r_runtime_darwin_io_request_set_completion",
        "runtime/darwin/source/io_request.c",
        "runtime-io",
    ),
    (
        "r_runtime_darwin_io_request_set_completion_inline",
        "runtime/darwin/source/io_request.c",
        "runtime-io",
    ),
    ("r_runtime_darwin_io_request_state", "runtime/darwin/source/io_request.c", "runtime-io"),
    (
        "r_runtime_darwin_io_request_take_buffer",
        "runtime/darwin/source/io_request.c",
        "runtime-io",
    ),
    ("r_runtime_darwin_io_request_wait", "runtime/darwin/source/io_request.c", "runtime-io"),
    ("r_runtime_darwin_io_submit_close", "runtime/darwin/source/io_close.c", "runtime-io"),
    ("r_runtime_darwin_io_submit_flush", "runtime/darwin/source/io_flush.c", "runtime-io"),
    ("r_runtime_darwin_io_submit_read", "runtime/darwin/source/io_read.c", "runtime-io"),
    ("r_runtime_darwin_io_submit_read_some", "runtime/darwin/source/io_read.c", "runtime-io"),
    ("r_runtime_darwin_io_submit_write", "runtime/darwin/source/io_write.c", "runtime-io"),
    ("r_runtime_darwin_io_submit_write_some", "runtime/darwin/source/io_write.c", "runtime-io"),
    ("r_runtime_dict_clear", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_contains", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_destroy_count", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_destroy_finish", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_destroy_key", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_destroy_locate", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_destroy_scratch", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_destroy_value", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_get", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_get_mut", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_initialize", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_remove", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_dict_reserve", "runtime/source/dict.c", "runtime-container"),
    ("r_runtime_executor_cancel_pending", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_executor_join_begin", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_executor_join_end", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_executor_lifecycle_start", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_executor_lifecycle_stop", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_executor_on_worker", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_executor_quiesce_for_exit", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_hosted_exit", "runtime/source/hosted_entry.c", "runtime-hosted"),
    ("r_runtime_hosted_work_begin", "runtime/source/thread_attachment.c", "runtime-hosted"),
    ("r_runtime_hosted_work_drain", "runtime/source/thread_attachment.c", "runtime-hosted"),
    ("r_runtime_hosted_work_end", "runtime/source/thread_attachment.c", "runtime-hosted"),
    ("r_runtime_list_back", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_back_mut", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_clear", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_destroy_finish", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_destroy_is_last", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_destroy_last", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_destroy_release_last", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_destroy_scratch", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_front", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_front_mut", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_get", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_get_mut", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_insert_after", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_insert_before", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_pop_back", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_pop_front", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_push_front", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_list_remove", "runtime/source/list.c", "runtime-container"),
    ("r_runtime_own_into_value", "runtime/source/own.c", "runtime-owner"),
    ("r_runtime_panic_category_name", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_panic_deliver", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_panic_resume", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_panic_second", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_panic_set_sink", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_panic_take", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_panic_terminate", "runtime/source/panic_abort.c", "runtime-panic"),
    ("r_runtime_rc_destroy_begin", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_destroy_finish", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_destroy_scratch", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_destroy_value", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_downgrade", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_from_raw", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_get_mut", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_into_raw", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_ptr_eq", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_strong_count", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_try_unwrap", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_rc_weak_count", "runtime/source/rc.c", "runtime-owner"),
    ("r_runtime_stack_can_require", "runtime/darwin/source/stack.c", "runtime-stack"),
    ("r_runtime_string_append", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_append_utf8", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_bytes", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_capacity", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_clear", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_from_bytes", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_from_utf8", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_initialize", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_into_bytes", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_length", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_push_scalar", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_reserve", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_truncate", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_string_with_capacity", "runtime/source/string.c", "runtime-container"),
    ("r_runtime_task_cancel", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_current_id", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_detach", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_external_acknowledge", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_external_acknowledge_panic", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_external_cancel_requested", "runtime/darwin/source/task.c", "runtime-task"),
    (
        "r_runtime_task_external_cancellation_sequence",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_external_select_terminal_completion",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    ("r_runtime_task_external_start_prepare", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_external_start_ready", "runtime/darwin/source/task.c", "runtime-task"),
    (
        "r_runtime_task_external_try_select_completion",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    (
        "r_runtime_task_external_try_select_completion_at",
        "runtime/darwin/source/task.c",
        "runtime-task",
    ),
    ("r_runtime_task_start_abort", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_start_allocator", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_start_commit", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_start_prepare", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_task_state", "runtime/darwin/source/task.c", "runtime-task"),
    ("r_runtime_thread_attach", "runtime/source/thread_attachment.c", "runtime-thread"),
    ("r_runtime_thread_lifecycle_start", "runtime/source/thread_attachment.c", "runtime-thread"),
    ("r_runtime_thread_lifecycle_stop", "runtime/source/thread_attachment.c", "runtime-thread"),
    (
        "r_runtime_thread_local_cleanup_current",
        "runtime/source/thread_attachment.c",
        "runtime-thread",
    ),
    ("r_runtime_weak_arc_upgrade", "runtime/source/arc.c", "runtime-owner"),
    ("r_runtime_weak_rc_upgrade", "runtime/source/rc.c", "runtime-owner"),
    ("r_std_array_as_slice", "library/std/array/source/as_slice.c", "stdlib-operation"),
    ("r_std_array_as_slice_mut", "library/std/array/source/as_slice_mut.c", "stdlib-operation"),
    ("r_std_array_get", "library/std/array/source/get.c", "stdlib-operation"),
    ("r_std_array_get_mut", "library/std/array/source/get_mut.c", "stdlib-operation"),
    ("r_std_array_pop", "library/std/array/source/pop.c", "stdlib-operation"),
    ("r_std_c_runtime_as_error", "library/std/c/source/runtime_as_error.c", "stdlib-operation"),
    ("r_std_c_string_as_error", "library/std/c/source/string_as_error.c", "stdlib-operation"),
    ("r_std_dict_contains", "library/std/dict/source/contains.c", "stdlib-operation"),
    ("r_std_dict_get", "library/std/dict/source/get.c", "stdlib-operation"),
    ("r_std_dict_get_mut", "library/std/dict/source/get_mut.c", "stdlib-operation"),
    ("r_std_env_as_error", "library/std/env/source/as_error.c", "stdlib-operation"),
    ("r_std_error_from_address", "library/std/error/source/from_address.c", "stdlib-operation"),
    ("r_std_error_from_alloc", "library/std/error/source/from_alloc.c", "stdlib-operation"),
    ("r_std_error_from_async", "library/std/error/source/from_async.c", "stdlib-operation"),
    ("r_std_error_from_barrier", "library/std/error/source/from_barrier.c", "stdlib-operation"),
    ("r_std_error_from_boundary", "library/std/error/source/from_boundary.c", "stdlib-operation"),
    ("r_std_error_from_bytes", "library/std/error/source/from_bytes.c", "stdlib-operation"),
    ("r_std_error_from_duration", "library/std/error/source/from_duration.c", "stdlib-operation"),
    ("r_std_error_from_format", "library/std/error/source/from_format.c", "stdlib-operation"),
    ("r_std_error_from_parse", "library/std/error/source/from_parse.c", "stdlib-operation"),
    ("r_std_error_from_path", "library/std/error/source/from_path.c", "stdlib-operation"),
    ("r_std_error_from_range", "library/std/error/source/from_range.c", "stdlib-operation"),
    ("r_std_error_from_string", "library/std/error/source/from_string.c", "stdlib-operation"),
    ("r_std_error_from_thread", "library/std/error/source/from_thread.c", "stdlib-operation"),
    ("r_std_list_back", "library/std/list/source/back.c", "stdlib-operation"),
    ("r_std_list_back_mut", "library/std/list/source/back_mut.c", "stdlib-operation"),
    ("r_std_list_front", "library/std/list/source/front.c", "stdlib-operation"),
    ("r_std_list_front_mut", "library/std/list/source/front_mut.c", "stdlib-operation"),
    ("r_std_list_get", "library/std/list/source/get.c", "stdlib-operation"),
    ("r_std_list_get_mut", "library/std/list/source/get_mut.c", "stdlib-operation"),
    ("r_std_list_insert_after", "library/std/list/source/insert_after.c", "stdlib-operation"),
    ("r_std_list_insert_before", "library/std/list/source/insert_before.c", "stdlib-operation"),
    ("r_std_list_pop_back", "library/std/list/source/pop_back.c", "stdlib-operation"),
    ("r_std_list_pop_front", "library/std/list/source/pop_front.c", "stdlib-operation"),
    ("r_std_list_push_back", "library/std/list/source/push_back.c", "stdlib-operation"),
    ("r_std_list_push_front", "library/std/list/source/push_front.c", "stdlib-operation"),
    ("r_std_list_remove", "library/std/list/source/remove.c", "stdlib-operation"),
    ("r_std_net_as_error", "library/std/net/source/as_error.c", "stdlib-operation"),
    ("r_std_process_as_error", "library/std/process/source/as_error.c", "stdlib-operation"),
    ("r_std_string_as_bytes", "library/std/string/source/as_bytes.c", "stdlib-operation"),
    ("r_std_string_capacity", "library/std/string/source/capacity.c", "stdlib-operation"),
    ("r_std_string_len", "library/std/string/source/len.c", "stdlib-operation"),
    ("r_std_thread_spawn", "library/std/thread/source/spawn.c", "stdlib-operation"),
    ("r_std_thread_spawn_scoped", "library/std/thread/source/spawn_scoped.c", "stdlib-operation"),
)

HEADER_HELPERS = (
    (
        "r_std_signal_listener_destroy",
        "library/std/signal/include/r_std_signal.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_signal_listener_move_initialize",
        "library/std/signal/include/r_std_signal.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_process_child_destroy",
        "library/std/process/include/r_std_process.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_process_child_move_initialize",
        "library/std/process/include/r_std_process.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_process_command_destroy",
        "library/std/process/include/r_std_process.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_process_command_move_initialize",
        "library/std/process/include/r_std_process.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_process_spawn_result_destroy",
        "library/std/process/include/r_std_process.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_process_spawn_result_move_initialize",
        "library/std/process/include/r_std_process.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_process_wait_result_destroy",
        "library/std/process/include/r_std_process.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_process_wait_result_move_initialize",
        "library/std/process/include/r_std_process.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_connection_destroy",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_connection_move_initialize",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_listener_destroy",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_listener_move_initialize",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_read_result_destroy",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_read_result_move_initialize",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_write_all_result_destroy",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_write_all_result_move_initialize",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_write_result_destroy",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_tcp_write_result_move_initialize",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_udp_receive_result_destroy",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_udp_receive_result_move_initialize",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_udp_send_result_destroy",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_udp_send_result_move_initialize",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_udp_socket_destroy",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_net_udp_socket_move_initialize",
        "library/std/net/include/r_std_net.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_channel_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_channel_move_initialize",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_receiver_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_receiver_move_initialize",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_sender_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_sender_move_initialize",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_sync_channel_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_sync_channel_move_initialize",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_sync_sender_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_sync_sender_move_initialize",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    ("r_std_sync_mutex_destroy", "library/std/sync/include/r_std_sync.h", "type-glue-header-helper"),
    (
        "r_std_sync_mutex_guard_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    ("r_std_sync_rw_lock_destroy", "library/std/sync/include/r_std_sync.h", "type-glue-header-helper"),
    (
        "r_std_sync_rw_read_guard_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_rw_write_guard_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    ("r_std_string_move_initialize", "library/std/string/include/r_std_string.h", "type-glue-header-helper"),
    ("r_std_string_destroy", "library/std/string/include/r_std_string.h", "type-glue-header-helper"),
    (
        "r_std_fs_directory_destroy",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_directory_entry_destroy",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_directory_entry_move_initialize",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_directory_iter_destroy",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_directory_iter_move_initialize",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_directory_move_initialize",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_directory_next_result_destroy",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_directory_next_result_move_initialize",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_file_destroy",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_file_move_initialize",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_path_destroy",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_path_move_initialize",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_write_file_result_destroy",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_fs_write_file_result_move_initialize",
        "library/std/fs/include/r_std_fs.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_input_destroy",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_input_move_initialize",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_output_destroy",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_output_move_initialize",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_read_result_destroy",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_read_result_move_initialize",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_shared_write_result_destroy",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_shared_write_result_move_initialize",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_write_result_destroy",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_write_result_move_initialize",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_write_all_result_destroy",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_io_write_all_result_move_initialize",
        "library/std/io/include/r_std_io.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_once_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_once_move_initialize",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_once_lock_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
)

HEADER_HELPERS += (
    ("r_std_c_string_destroy", "library/std/c/include/r_std_c.h", "type-glue-header-helper"),
    ("r_std_c_string_move_initialize", "library/std/c/include/r_std_c.h", "type-glue-header-helper"),
    ("r_std_c_handle_destroy", "library/std/c/include/r_std_c.h", "type-glue-header-helper"),
    ("r_std_c_handle_move_initialize", "library/std/c/include/r_std_c.h", "type-glue-header-helper"),
    ("r_std_c_thread_attachment_destroy", "library/std/c/include/r_std_c.h", "type-glue-header-helper"),
    ("r_std_c_thread_attachment_move_initialize", "library/std/c/include/r_std_c.h", "type-glue-header-helper"),
    ("r_std_secret_buffer_destroy", "library/std/secret/include/r_std_secret.h", "type-glue-header-helper"),
    ("r_std_secret_buffer_move_initialize", "library/std/secret/include/r_std_secret.h", "type-glue-header-helper"),
    ("r_std_net_tcp_stream_move_initialize", "library/std/net/include/r_std_net.h", "type-glue-header-helper"),
    ("r_std_net_tcp_stream_destroy", "library/std/net/include/r_std_net.h", "type-glue-header-helper"),
    ("r_std_net_unix_listener_move_initialize", "library/std/net/include/r_std_net.h", "type-glue-header-helper"),
    ("r_std_net_unix_listener_destroy", "library/std/net/include/r_std_net.h", "type-glue-header-helper"),
    ("r_std_net_unix_stream_move_initialize", "library/std/net/include/r_std_net.h", "type-glue-header-helper"),
    ("r_std_net_unix_stream_destroy", "library/std/net/include/r_std_net.h", "type-glue-header-helper"),
    ("r_std_net_unix_datagram_move_initialize", "library/std/net/include/r_std_net.h", "type-glue-header-helper"),
    ("r_std_net_unix_datagram_destroy", "library/std/net/include/r_std_net.h", "type-glue-header-helper"),
    ("r_std_json_reader_move_initialize", "library/std/json/include/r_std_json_reader.h", "type-glue-header-helper"),
    ("r_std_json_reader_destroy", "library/std/json/include/r_std_json_reader.h", "type-glue-header-helper"),
    ("r_std_json_detached_move_initialize", "library/std/json/include/r_std_json_reader.h", "type-glue-header-helper"),
    ("r_std_json_detached_destroy", "library/std/json/include/r_std_json_reader.h", "type-glue-header-helper"),
    ("r_std_json_decoder_move_initialize", "library/std/json/include/r_std_json.h", "type-glue-header-helper"),
    ("r_std_json_decoder_destroy", "library/std/json/include/r_std_json.h", "type-glue-header-helper"),
    ("r_std_json_value_move_initialize", "library/std/json/include/r_std_json.h", "type-glue-header-helper"),
    ("r_std_json_value_destroy", "library/std/json/include/r_std_json.h", "type-glue-header-helper"),
    ("r_std_json_number_move_initialize", "library/std/json/include/r_std_json.h", "type-glue-header-helper"),
    ("r_std_json_number_destroy", "library/std/json/include/r_std_json.h", "type-glue-header-helper"),
    ("r_std_json_error_move_initialize", "library/std/json/include/r_std_json.h", "type-glue-header-helper"),
    ("r_std_json_error_destroy", "library/std/json/include/r_std_json.h", "type-glue-header-helper"),
    ("r_std_string_from_bytes_result_destroy", "library/std/string/include/r_std_string.h", "type-glue-header-helper"),
    ("r_std_string_from_bytes_result_move_initialize", "library/std/string/include/r_std_string.h", "type-glue-header-helper"),
    ("r_std_sync_barrier_destroy", "library/std/sync/include/r_std_sync.h", "type-glue-header-helper"),
    ("r_std_sync_barrier_move_initialize", "library/std/sync/include/r_std_sync.h", "type-glue-header-helper"),
    ("r_std_sync_condvar_destroy", "library/std/sync/include/r_std_sync.h", "type-glue-header-helper"),
    ("r_std_sync_condvar_move_initialize", "library/std/sync/include/r_std_sync.h", "type-glue-header-helper"),
    ("r_std_format_builder_destroy", "library/std/format/include/r_std_format.h", "type-glue-header-helper"),
    ("r_std_format_builder_move_initialize", "library/std/format/include/r_std_format.h", "type-glue-header-helper"),
    ("r_std_thread_join_result_destroy", "library/std/thread/include/r_std_thread.h", "type-glue-header-helper"),
    ("r_std_thread_panic_report_destroy", "library/std/thread/include/r_std_thread.h", "type-glue-header-helper"),
    ("r_std_thread_panic_report_move_initialize", "library/std/thread/include/r_std_thread.h", "type-glue-header-helper"),
    ("r_std_thread_thread_destroy", "library/std/thread/include/r_std_thread.h", "type-glue-header-helper"),
    ("r_std_thread_thread_move_initialize", "library/std/thread/include/r_std_thread.h", "type-glue-header-helper"),
)

HEADER_HELPERS += (
    (
        "r_std_async_mutex_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_mutex_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_mutex_guard_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_mutex_guard_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_rw_lock_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_rw_lock_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_rw_read_guard_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_rw_read_guard_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_rw_write_guard_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_rw_write_guard_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_semaphore_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_semaphore_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_semaphore_permit_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_semaphore_permit_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_notify_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_notify_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_broadcast_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_broadcast_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_broadcast_receiver_destroy",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_async_broadcast_receiver_move_initialize",
        "library/std/async/include/r_std_async.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_permit_destroy",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
    (
        "r_std_sync_permit_move_initialize",
        "library/std/sync/include/r_std_sync.h",
        "type-glue-header-helper",
    ),
)

STACK_USAGE_PATTERN = re.compile(
    r"^(?P<source>.+?):[0-9]+(?::[0-9]+)?:"
    r"(?P<symbol>[A-Za-z_][A-Za-z0-9_]*)\t"
    r"(?P<size>[0-9]+)\t(?P<kind>.+)$"
)
GENERATED_PROJECT_SYMBOL_PATTERN = re.compile(
    r"\br_(?:library_internal|runtime|std|json)_[a-z0-9_]*[a-z0-9]\b"
)
HEADER_HELPER_START_PATTERN = re.compile(
    r"\bstatic\s+inline\s+(?:void|_Bool)\s+"
    r"(?P<symbol>r_std_(?:async|c|format|fs|io|net|process|secret|signal|sync|string|thread|json)_[a-z0-9_]+"
    r"|r_runtime_unwinding)\s*"
    r"\([^{};]*\)\s*\{",
    re.DOTALL,
)
HEADER_DOWNSTREAM_CALL_PATTERN = re.compile(
    r"\b(?P<symbol>r_(?:runtime|library_internal|json)_[a-z0-9_]+)\s*\("
)

# The names of functions the LLVM emitter defines itself (JSON glue) and the panic counter
# it reads (r_runtime_unwinding_threads, an object, not an entry).
GENERATED_NON_CALL_IDENTIFIERS = frozenset(
    {
        "r_json_decode",
        "r_json_default",
        "r_json_encode",
        "r_json_reader_complete",
        "r_json_reader_deadline",
        "r_json_reader_start",
        "r_json_reader_take",
        "r_json_stream",
        "r_json_stream_drop",
        "r_json_stream_step",
        "r_json_zero",
        "r_runtime_unwinding_threads",
    }
)
MOVE_REGISTRY_NON_CALL_IDENTIFIERS = frozenset({"r_std_async", "r_std_c", "r_std_format", "r_std_fs", "r_std_io", "r_std_json", "r_std_json_reader", "r_std_net", "r_std_process", "r_std_secret", "r_std_signal", "r_std_string", "r_std_sync", "r_std_thread"})
HEADER_SOURCE_PATHS = (
    "runtime/include/r_runtime_0_1.h",
    "library/std/async/include/r_std_async.h",
    "library/std/c/include/r_std_c.h",
    "library/std/format/include/r_std_format.h",
    "library/std/fs/include/r_std_fs.h",
    "library/std/io/include/r_std_io.h",
    "library/std/sync/include/r_std_sync.h",
    "library/std/string/include/r_std_string.h",
    "library/std/json/include/r_std_json.h",
    "library/std/json/include/r_std_json_reader.h",
    "library/std/net/include/r_std_net.h",
    "library/std/process/include/r_std_process.h",
    "library/std/secret/include/r_std_secret.h",
    "library/std/signal/include/r_std_signal.h",
    "library/std/thread/include/r_std_thread.h",
)
MATH_OPERATION_DESCRIPTOR_PATH = "compiler/source/standard_math_operations.generated.inc"
# B6: the program emitter is compiled/llvm; a call target is a string literal there.
GENERATED_LITERAL_PATTERN = re.compile(r'"(r_(?:library_internal|runtime|std|json)_[a-z0-9_]*[a-z0-9])\b')
GENERATED_DESCRIPTOR_PATHS = (
    "compiler/source/standard_async_sync.h",
    "compiler/source/standard_fs_async.h",
    "compiler/source/standard_scoped_operations.h",
    "compiler/source/standard_sync.h",
    MATH_OPERATION_DESCRIPTOR_PATH,
)
MATH_OPERATION_SYMBOL_PATTERN = re.compile(r'"(?P<symbol>r_std_math_[a-z0-9_]+)"')


def entry_record(symbol: str, source: str, category: str, linkage: str) -> dict[str, Any]:
    return {
        "c_symbol": symbol,
        "source": source,
        "category": category,
        "linkage": linkage,
        "direct_frame_measurement_available": True,
    }


def project_external_entries(root: Path) -> tuple[tuple[str, str, str], ...]:
    records = {record[0]: record for record in EXTERNAL_ENTRIES}
    descriptor_source = (root / MATH_OPERATION_DESCRIPTOR_PATH).read_text(encoding="utf-8")
    for match in MATH_OPERATION_SYMBOL_PATTERN.finditer(descriptor_source):
        symbol = match.group("symbol")
        suffix = symbol.removeprefix("r_std_math_")
        records[symbol] = (
            symbol,
            f"library/std/math/source/{suffix}.c",
            "stdlib-operation",
        )
    return tuple(records.values())


def build_inventory(root: Path) -> dict[str, Any]:
    external = [
        entry_record(*record, "external") for record in sorted(project_external_entries(root))
    ]
    helpers = [entry_record(*record, "header-static-inline") for record in sorted(HEADER_HELPERS)]
    return {
        "schema": SCHEMA,
        "inventory_revision": INVENTORY_REVISION,
        "target": {
            "name": TARGET_NAME,
            "manifest": TARGET_MANIFEST,
            "manifest_revision": TARGET_MANIFEST_REVISION,
            "compiler": COMPILER,
            "compiler_version": COMPILER_VERSION,
            "compiler_build": COMPILER_BUILD,
        },
        "conformance_claim": False,
        "coverage": {
            "scope": "direct-project-entry-frame-only",
            "production_linking_uses_measured_objects": False,
            "transitive_project_call_chain": "not-measured",
            "native_system_library_frames": "not-measured",
            "foreign_provider_frames": "not-measured",
            "startup_before_stack_bounds": "not-measured",
            "instrumented_builds": "diagnostic-only",
            "call_transition_bytes": 16384,
            "call_transition_role": "unmeasured-downstream-reserve",
        },
        "measurement": {
            "format": "clang-stack-usage",
            "compile_flags": list(COMPILE_FLAGS),
            "accepted_frame_kind": "static",
            "frame_ceiling_bytes": FRAME_CEILING_BYTES,
            "maximum_input_reports": MAXIMUM_INPUT_REPORTS,
            "maximum_input_records": MAXIMUM_INPUT_RECORDS,
            "maximum_input_bytes_per_report": MAXIMUM_INPUT_BYTES_PER_REPORT,
            "macro_format": "R_RUNTIME_ENTRY_FRAME_<c_symbol>",
            "ordering": "c-symbol-ascending",
            "missing_duplicate_nonstatic_wrong_source": "hard-fail",
            "header_without_stack_usage_inputs": "hard-fail",
        },
        "closed_source_surfaces": {
            "program_emitter": "compiler/llvm",
            "program_emitter_sources": list(generated_emitter_paths(root)),
            "program_emitter_non_call_identifiers": sorted(GENERATED_NON_CALL_IDENTIFIERS),
            "named_standard_move_registry": (
                "compiler/source/named_standard_move_abi.generated.inc"
            ),
            "named_standard_move_registry_non_call_identifiers": sorted(
                MOVE_REGISTRY_NON_CALL_IDENTIFIERS
            ),
            "header_static_inline_helpers": [
                *HEADER_SOURCE_PATHS,
            ],
            "new_or_missing_project_entry": "hard-fail",
        },
        "external_entries": external,
        "header_static_inline_helpers": helpers,
    }


def canonical_json(value: Any) -> bytes:
    return (json.dumps(value, sort_keys=True, separators=(",", ":")) + "\n").encode("utf-8")


def require(condition: bool, message: str) -> None:
    if not condition:
        raise ValueError(message)


def generated_emitter_paths(root: Path) -> tuple[str, ...]:
    """The LLVM emitter and the descriptor headers it shares with the semantic analysis."""
    emitter = tuple(
        str(path.relative_to(root))
        for pattern in ("*.c", "*.h", "*.inc")
        for path in sorted((root / "compiler/llvm").glob(pattern))
    )
    return (
        *emitter,
        *GENERATED_DESCRIPTOR_PATHS,
    )


def generated_emitter_source(root: Path) -> str:
    """The text the emitted program's calls are named in: every string literal of the LLVM
    emitter that starts with a project prefix (the emitter names each runtime or library entry
    it calls by a literal, and r_std_net_<name> through its descriptor table), and the
    descriptor headers whole."""
    literals = []
    for path in generated_emitter_paths(root):
        text = (root / path).read_text(encoding="utf-8")
        if path.startswith("compiler/llvm/"):
            literals.extend(GENERATED_LITERAL_PATTERN.findall(text))
        else:
            literals.append(text)
    return "\n".join(literals)


def generated_direct_symbols(root: Path) -> set[str]:
    source = generated_emitter_source(root)
    symbols = set(GENERATED_PROJECT_SYMBOL_PATTERN.findall(source))
    header_helpers = {record[0] for record in HEADER_HELPERS}
    require_exact_symbols(
        "generated non-call identifier allowlist",
        set(GENERATED_NON_CALL_IDENTIFIERS),
        symbols & set(GENERATED_NON_CALL_IDENTIFIERS),
    )
    return symbols - set(GENERATED_NON_CALL_IDENTIFIERS) - header_helpers


def move_registry_helper_symbols(root: Path) -> set[str]:
    source = (
        root / "compiler/source/named_standard_move_abi.generated.inc"
    ).read_text(encoding="utf-8")
    symbols = set(GENERATED_PROJECT_SYMBOL_PATTERN.findall(source))
    require_exact_symbols(
        "named move registry non-call identifier allowlist",
        set(MOVE_REGISTRY_NON_CALL_IDENTIFIERS),
        symbols & set(MOVE_REGISTRY_NON_CALL_IDENTIFIERS),
    )
    return symbols - set(MOVE_REGISTRY_NON_CALL_IDENTIFIERS)


def header_helper_bodies(source: str) -> Iterable[tuple[str, str]]:
    """Yield static-inline compiler helper bodies, including bodies with nested blocks."""
    for match in HEADER_HELPER_START_PATTERN.finditer(source):
        body_start = match.end()
        cursor = body_start
        depth = 1
        state = "normal"
        while cursor < len(source) and depth != 0:
            character = source[cursor]
            following = source[cursor + 1] if cursor + 1 < len(source) else ""
            if state == "line-comment":
                if character == "\n":
                    state = "normal"
            elif state == "block-comment":
                if character == "*" and following == "/":
                    state = "normal"
                    cursor += 1
            elif state == "string":
                if character == "\\":
                    cursor += 1
                elif character == '"':
                    state = "normal"
            elif state == "character":
                if character == "\\":
                    cursor += 1
                elif character == "'":
                    state = "normal"
            elif character == "/" and following == "/":
                state = "line-comment"
                cursor += 1
            elif character == "/" and following == "*":
                state = "block-comment"
                cursor += 1
            elif character == '"':
                state = "string"
            elif character == "'":
                state = "character"
            elif character == "{":
                depth += 1
            elif character == "}":
                depth -= 1
            cursor += 1
        require(depth == 0, f"unterminated static-inline helper {match.group('symbol')}")
        yield match.group("symbol"), source[body_start : cursor - 1]


def header_helper_symbols(root: Path) -> set[str]:
    symbols: set[str] = set()
    generated = generated_emitter_source(root)
    referenced = move_registry_helper_symbols(root) | set(
        GENERATED_PROJECT_SYMBOL_PATTERN.findall(generated)
    )
    for source_path in HEADER_SOURCE_PATHS:
        source = (root / source_path).read_text(encoding="utf-8")
        symbols.update(
            symbol for symbol, _ in header_helper_bodies(source) if symbol in referenced
        )
    return symbols


def header_downstream_symbols(root: Path) -> set[str]:
    symbols: set[str] = set()
    generated = generated_emitter_source(root)
    referenced = move_registry_helper_symbols(root) | set(
        GENERATED_PROJECT_SYMBOL_PATTERN.findall(generated)
    )
    for source_path in HEADER_SOURCE_PATHS:
        source = (root / source_path).read_text(encoding="utf-8")
        for symbol, body in header_helper_bodies(source):
            if symbol not in referenced:
                continue
            symbols.update(
                match.group("symbol") for match in HEADER_DOWNSTREAM_CALL_PATTERN.finditer(body)
            )
    return symbols


def require_exact_symbols(label: str, expected: set[str], observed: set[str]) -> None:
    missing = sorted(expected - observed)
    new = sorted(observed - expected)
    require(
        not missing and not new,
        f"{label} is not closed; missing={','.join(missing) or '-'}; "
        f"new={','.join(new) or '-'}",
    )


def validate_source_surfaces(root: Path) -> None:
    require(
        len(EXTERNAL_ENTRIES) == len({record[0] for record in EXTERNAL_ENTRIES}),
        "duplicate external entry in built-in catalog",
    )
    require(
        len(HEADER_HELPERS) == len({record[0] for record in HEADER_HELPERS}),
        "duplicate header helper in built-in catalog",
    )
    entries = project_external_entries(root)
    direct = {
        symbol
        for symbol, _, category in entries
        if category != "type-glue-downstream"
    }
    downstream = {
        symbol
        for symbol, _, category in entries
        if category == "type-glue-downstream"
    }
    helpers = {symbol for symbol, _, _ in HEADER_HELPERS}
    registry_helpers = move_registry_helper_symbols(root)
    require_exact_symbols(
        "generated project entry surface", direct, generated_direct_symbols(root)
    )
    require_exact_symbols("header static-inline helper surface", helpers, header_helper_symbols(root))
    require_exact_symbols(
        "named move registry helper surface", registry_helpers & helpers, registry_helpers
    )
    observed_downstream = header_downstream_symbols(root)
    require_exact_symbols(
        "header helper downstream surface",
        downstream | (direct & observed_downstream),
        observed_downstream,
    )


def load_inventory(path: Path, root: Path) -> tuple[dict[str, Any], bytes]:
    validate_source_surfaces(root)
    raw = path.read_bytes()
    value = json.loads(raw)
    require(isinstance(value, dict), "runtime entry stack inventory root must be an object")
    expected = build_inventory(root)
    require(value == expected, "runtime entry stack inventory is not the closed catalog")

    manifest_path = root / TARGET_MANIFEST
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    require(manifest.get("manifest_revision") == TARGET_MANIFEST_REVISION,
            "runtime entry inventory target manifest revision is stale")
    require(manifest.get("conformance_claim") is False,
            "draft target must not claim conformance")
    toolchain = manifest.get("toolchain")
    require(isinstance(toolchain, dict), "target toolchain must be an object")
    require(toolchain.get("c_compiler") == COMPILER,
            "runtime entry inventory compiler does not match target")
    require(toolchain.get("c_compiler_version") == COMPILER_VERSION,
            "runtime entry inventory compiler version does not match target")
    require(toolchain.get("c_compiler_build") == COMPILER_BUILD,
            "runtime entry inventory compiler build does not match target")

    records = value["external_entries"] + value["header_static_inline_helpers"]
    symbols: set[str] = set()
    for record in records:
        symbol = record["c_symbol"]
        source = root / record["source"]
        require(symbol not in symbols, f"duplicate inventory symbol: {symbol}")
        symbols.add(symbol)
        require(source.is_file(), f"inventory source does not exist: {record['source']}")
        text = source.read_text(encoding="utf-8")
        require(re.search(rf"\b{re.escape(symbol)}\s*\(", text) is not None,
                f"inventory symbol is absent from source: {symbol}")
    return value, canonical_json(value)


def normalized_source(source: str, root: Path) -> str:
    candidate = Path(source)
    if candidate.is_absolute():
        resolved = candidate.resolve()
        try:
            return resolved.relative_to(root).as_posix()
        except ValueError as error:
            raise ValueError(f"stack-usage source is outside repository: {source}") from error
    normalized = Path(source).as_posix()
    while normalized.startswith("./"):
        normalized = normalized[2:]
    require(normalized != "" and not normalized.startswith("../"),
            f"invalid stack-usage source path: {source}")
    return normalized


def expected_records(inventory: dict[str, Any]) -> dict[str, dict[str, Any]]:
    records = inventory["external_entries"] + inventory["header_static_inline_helpers"]
    return {record["c_symbol"]: record for record in records}


def read_measurements(
    paths: Iterable[Path], inventory: dict[str, Any], root: Path
) -> dict[str, tuple[str, int]]:
    reports = list(paths)
    require(reports, "at least one real .su stack-usage input is required")
    require(len(reports) <= MAXIMUM_INPUT_REPORTS,
            f"stack-usage input report cap exceeded: {len(reports)} > {MAXIMUM_INPUT_REPORTS}")
    expected = expected_records(inventory)
    measured: dict[str, tuple[str, int]] = {}
    record_count = 0

    for report in reports:
        require(report.suffix == ".su", f"stack-usage input must have .su suffix: {report}")
        require(report.is_file(), f"stack-usage input does not exist: {report}")
        size = report.stat().st_size
        require(size > 0, f"stack-usage input is empty: {report}")
        require(size <= MAXIMUM_INPUT_BYTES_PER_REPORT,
                f"stack-usage input byte cap exceeded: {report}")
        for line_number, line in enumerate(report.read_text(encoding="utf-8").splitlines(), 1):
            if line == "":
                continue
            record_count += 1
            require(record_count <= MAXIMUM_INPUT_RECORDS,
                    f"stack-usage input record cap exceeded: {record_count} > "
                    f"{MAXIMUM_INPUT_RECORDS}")
            match = STACK_USAGE_PATTERN.fullmatch(line)
            require(match is not None,
                    f"malformed stack-usage record at {report}:{line_number}")
            symbol = match.group("symbol")
            expected_record = expected.get(symbol)
            if expected_record is None:
                continue
            source = normalized_source(match.group("source"), root)
            require(source == expected_record["source"],
                    f"wrong source for {symbol}: {source} != {expected_record['source']}")
            require(match.group("kind") == "static",
                    f"non-static stack frame for {symbol}: {match.group('kind')}")
            frame_size = int(match.group("size"), 10)
            require(frame_size <= FRAME_CEILING_BYTES,
                    f"direct entry frame exceeds ceiling for {symbol}: "
                    f"{frame_size} > {FRAME_CEILING_BYTES}")
            require(symbol not in measured, f"duplicate stack-usage record for {symbol}")
            measured[symbol] = (source, frame_size)

    missing = sorted(set(expected) - set(measured))
    require(not missing, "missing stack-usage records: " + ", ".join(missing))
    return measured


def render_header(inventory_bytes: bytes, measured: dict[str, tuple[str, int]]) -> str:
    inventory_digest = hashlib.sha256(inventory_bytes).hexdigest()
    measurement_value = [
        {"c_symbol": symbol, "source": measured[symbol][0], "frame_bytes": measured[symbol][1]}
        for symbol in sorted(measured)
    ]
    measurement_digest = hashlib.sha256(canonical_json(measurement_value)).hexdigest()
    maximum = max(frame_size for _, frame_size in measured.values())
    lines = [
        "/* Generated by tools/generate_runtime_entry_stack.py. */",
        "/* Direct project entry frames only; transitive and native frames are not bounded. */",
        "#ifndef R_RUNTIME_ENTRY_STACK_ABI_H",
        "#define R_RUNTIME_ENTRY_STACK_ABI_H",
        "",
        "#include <stddef.h>",
        "",
        f'#define R_RUNTIME_ENTRY_STACK_INVENTORY_SHA256 "{inventory_digest}"',
        f'#define R_RUNTIME_ENTRY_STACK_MEASUREMENTS_SHA256 "{measurement_digest}"',
        "#define R_RUNTIME_ENTRY_STACK_CONFORMANCE_CLAIM 0",
        f"#define R_RUNTIME_ENTRY_STACK_ENTRY_COUNT ((size_t){len(measured)})",
        f"#define R_RUNTIME_ENTRY_STACK_MAX_DIRECT_FRAME_BYTES ((size_t){maximum})",
        "",
    ]
    for symbol in sorted(measured):
        lines.append(f"#define R_RUNTIME_ENTRY_FRAME_{symbol} ((size_t){measured[symbol][1]})")
    lines.extend(("", "#endif", ""))
    return "\n".join(lines)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, required=True)
    parser.add_argument("--inventory", type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write-inventory", action="store_true")
    mode.add_argument("--validate-inventory", action="store_true")
    mode.add_argument("--write-header", action="store_true")
    mode.add_argument("--verify-header", action="store_true")
    parser.add_argument("--output-header", type=Path)
    parser.add_argument("--compiler-build")
    parser.add_argument("--stack-usage", type=Path, action="append", default=[])
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    root = arguments.root.resolve()
    inventory_path = arguments.inventory.resolve()
    try:
        if arguments.write_inventory:
            require(not arguments.stack_usage and arguments.output_header is None,
                    "inventory generation does not accept stack-usage/header arguments")
            validate_source_surfaces(root)
            inventory_path.parent.mkdir(parents=True, exist_ok=True)
            inventory_path.write_text(
                json.dumps(build_inventory(root), indent=2) + "\n",
                encoding="utf-8",
                newline="\n",
            )
            return 0

        inventory, inventory_bytes = load_inventory(inventory_path, root)
        if arguments.validate_inventory:
            require(not arguments.stack_usage and arguments.output_header is None,
                    "inventory validation does not accept stack-usage/header arguments")
            return 0

        require(arguments.output_header is not None,
                "header generation/verification requires --output-header")
        require(arguments.compiler_build == COMPILER_BUILD,
                f"header measurement requires compiler build {COMPILER_BUILD}")
        measurements = read_measurements(arguments.stack_usage, inventory, root)
        rendered = render_header(inventory_bytes, measurements)
        output = arguments.output_header.resolve()
        if arguments.write_header:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_text(rendered, encoding="utf-8", newline="\n")
        else:
            require(output.is_file(), f"runtime entry stack header does not exist: {output}")
            require(output.read_text(encoding="utf-8") == rendered,
                    f"runtime entry stack header is stale: {output}")
        return 0
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as error:
        print(f"runtime entry stack error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
