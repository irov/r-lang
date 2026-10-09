#!/usr/bin/env python3
"""Run bounded ownership and compiler-memory-safety regression probes.

Invalid safety probes must be rejected by semantic analysis. Valid controls
must reach LLVM IR. Compiler-memory probes must finish without sanitizer findings.
The audit intentionally exits with status 1 while any safety defect is present.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


SEMANTIC_PROBES = (
    (
        "shared_borrow_then_move_own_same_origin",
        "audit_call_borrow_then_move_own_same_origin.r",
        "rejected",
        ("R-OWN-0002", "R-OWN-0009", "R-BORROW-0002"),
    ),
    (
        "exclusive_borrow_then_move_own_same_origin",
        "audit_call_exclusive_borrow_then_move_own_same_origin.r",
        "rejected",
        ("R-OWN-0002", "R-OWN-0009", "R-BORROW-0002"),
    ),
    (
        "generic_borrow_then_move_own_same_origin",
        "audit_generic_call_borrow_then_move_own_same_origin.r",
        "rejected",
        ("R-OWN-0002", "R-OWN-0009", "R-BORROW-0002"),
    ),
    (
        "shared_borrow_then_move_rc_same_origin",
        "audit_call_borrow_then_move_rc_same_origin.r",
        "rejected",
        ("R-OWN-0010", "R-BORROW-0002"),
    ),
    (
        "borrow_then_move_own_distinct_origins",
        "audit_call_borrow_then_move_own_distinct_origins.r",
        "accepted",
        ("R-OWN-0002", "R-BORROW-0002"),
    ),
    (
        "exclusive_borrow_then_read_same_origin",
        "audit_call_exclusive_borrow_then_read_same_origin.r",
        "rejected",
        ("R-AM-0009", "R-BORROW-0002", "R-EXPR-0020"),
    ),
    (
        "borrow_then_conditional_move_own_same_origin",
        "audit_call_borrow_then_conditional_move_same_origin.r",
        "rejected",
        ("R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-EXPR-0014"),
    ),
    (
        "borrow_then_aggregate_move_own_same_origin",
        "audit_call_borrow_then_aggregate_move_same_origin.r",
        "rejected",
        ("R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-EXPR-0020"),
    ),
    (
        "borrow_then_variant_move_own_same_origin",
        "audit_call_borrow_then_variant_move_same_origin.r",
        "rejected",
        ("R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-EXPR-0020"),
    ),
    (
        "borrow_then_new_aggregate_move_own_same_origin",
        "audit_call_borrow_then_new_aggregate_move_same_origin.r",
        "rejected",
        ("R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-EXPR-0020"),
    ),
    (
        "aggregate_initializer_borrow_then_move_same_origin",
        "audit_aggregate_initializer_borrow_then_move_same_origin.r",
        "rejected",
        ("R-AM-0009", "R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "generic_aggregate_initializer_borrow_then_move_same_origin",
        "audit_generic_aggregate_initializer_borrow_then_move_same_origin.r",
        "rejected",
        ("R-AM-0009", "R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "error_initializer_borrow_then_move_same_origin",
        "audit_error_initializer_borrow_then_move_same_origin.r",
        "rejected",
        ("R-AM-0009", "R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-ERR-0002"),
    ),
    (
        "aggregate_initializer_borrow_then_conditional_move_same_origin",
        "audit_aggregate_initializer_borrow_then_conditional_move_same_origin.r",
        "rejected",
        ("R-AM-0009", "R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-EXPR-0014"),
    ),
    (
        "aggregate_initializer_borrow_then_nested_move_same_origin",
        "audit_aggregate_initializer_borrow_then_nested_move_same_origin.r",
        "rejected",
        ("R-AM-0009", "R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-EXPR-0020"),
    ),
    (
        "aggregate_initializer_borrow_then_new_move_same_origin",
        "audit_aggregate_initializer_borrow_then_new_move_same_origin.r",
        "rejected",
        ("R-AM-0009", "R-OWN-0002", "R-OWN-0009", "R-BORROW-0002", "R-EXPR-0020"),
    ),
    (
        "aggregate_initializer_exclusive_borrow_then_read_same_origin",
        "audit_aggregate_initializer_exclusive_borrow_then_read_same_origin.r",
        "rejected",
        ("R-AM-0009", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "aggregate_initializer_borrow_then_move_distinct_origins",
        "audit_aggregate_initializer_borrow_then_move_distinct_origins.r",
        "accepted",
        ("R-AM-0009", "R-OWN-0002", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "aggregate_initializer_unreachable_move_same_origin",
        "audit_aggregate_initializer_unreachable_move_same_origin.r",
        "accepted",
        ("R-AM-0009", "R-OWN-0002", "R-BORROW-0002", "R-EXPR-0014"),
    ),
    (
        "aggregate_initializer_shared_borrow_then_read_same_origin",
        "audit_aggregate_initializer_shared_borrow_then_read_same_origin.r",
        "accepted",
        ("R-AM-0009", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "std_bytes_append_self_alias",
        "audit_std_bytes_append_self_alias.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-BYTES-0004"),
    ),
    (
        "async_std_bytes_append_self_alias",
        "audit_async_std_bytes_append_self_alias.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-BYTES-0004"),
    ),
    (
        "std_bytes_copy_overlapping_ranges",
        "audit_std_bytes_copy_overlapping_ranges.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-BYTES-0002"),
    ),
    (
        "std_bytes_fill_read_same_origin",
        "audit_std_bytes_fill_read_same_origin.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-BYTES-0002"),
    ),
    (
        "std_string_append_str_self_alias",
        "audit_std_string_append_str_self_alias.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-STRING-0003"),
    ),
    (
        "async_std_string_append_str_self_alias",
        "audit_async_std_string_append_str_self_alias.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-STRING-0003"),
    ),
    (
        "std_string_append_utf8_self_alias",
        "audit_std_string_append_utf8_self_alias.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-STRING-0003"),
    ),
    (
        "std_string_reserve_read_same_origin",
        "audit_std_string_reserve_read_same_origin.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-STRING-0003"),
    ),
    (
        "std_string_truncate_read_same_origin",
        "audit_std_string_truncate_read_same_origin.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-SLIB-STRING-0003"),
    ),
    (
        "std_array_reserve_read_same_origin",
        "audit_std_array_reserve_read_same_origin.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0003", "R-LIB-0019"),
    ),
    (
        "std_bytes_append_distinct_source",
        "audit_std_bytes_append_distinct_source.r",
        "accepted",
        ("R-BORROW-0002", "R-SLIB-BYTES-0004"),
    ),
    (
        "std_bytes_copy_disjoint_ranges",
        "audit_std_bytes_copy_disjoint_ranges.r",
        "accepted",
        ("R-BORROW-0002", "R-SLIB-BYTES-0002"),
    ),
    (
        "std_bytes_fill_read_distinct_origin",
        "audit_std_bytes_fill_read_distinct_origin.r",
        "accepted",
        ("R-BORROW-0002", "R-SLIB-BYTES-0002"),
    ),
    (
        "std_string_append_distinct_source",
        "audit_std_string_append_distinct_source.r",
        "accepted",
        ("R-BORROW-0002", "R-SLIB-STRING-0003"),
    ),
    (
        "std_string_mutation_read_distinct_origin",
        "audit_std_string_mutation_read_distinct_origin.r",
        "accepted",
        ("R-BORROW-0002", "R-SLIB-STRING-0003"),
    ),
    (
        "std_array_reserve_read_distinct_origin",
        "audit_std_array_reserve_read_distinct_origin.r",
        "accepted",
        ("R-BORROW-0002", "R-LIB-0019"),
    ),
    (
        "borrow_then_nested_move_own_distinct_origin",
        "audit_call_borrow_then_nested_move_distinct_origin.r",
        "accepted",
        ("R-OWN-0002", "R-BORROW-0002"),
    ),
    (
        "disjoint_field_borrows",
        "audit_call_disjoint_field_borrows.r",
        "accepted",
        ("R-BORROW-0002",),
    ),
    (
        "borrow_then_unreachable_nested_move",
        "audit_call_borrow_then_unreachable_nested_move.r",
        "accepted",
        ("R-OWN-0002", "R-BORROW-0002"),
    ),
    (
        "field_assignment_borrow_provenance_uaf",
        "audit_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0018"),
    ),
    (
        "field_assignment_slice_provenance_uaf",
        "audit_field_assignment_slice_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0018", "R-LIB-0019"),
    ),
    (
        "nested_field_assignment_borrow_provenance_uaf",
        "audit_nested_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0018"),
    ),
    (
        "index_assignment_borrow_provenance_uaf",
        "audit_index_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0018"),
    ),
    (
        "generic_field_assignment_borrow_provenance_uaf",
        "audit_generic_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0018", "R-TYPE-0032"),
    ),
    (
        "error_field_assignment_borrow_provenance_uaf",
        "audit_error_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0018", "R-ERR-0002"),
    ),
    (
        "async_field_assignment_borrow_provenance_uaf",
        "audit_async_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0018", "R-FUNC-0011"),
    ),
    (
        "throw_field_assignment_borrow_provenance_uaf",
        "audit_throw_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0018", "R-ERR-0002"),
    ),
    (
        "field_assignment_borrow_provenance_control",
        "audit_field_assignment_borrow_provenance_control.r",
        "accepted",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0018"),
    ),
    (
        "field_assignment_releases_previous_origin",
        "audit_field_assignment_releases_previous_origin.r",
        "accepted",
        ("R-INIT-0007", "R-INIT-0011", "R-OWN-0009", "R-BORROW-0018"),
    ),
    (
        "disjoint_internal_field_assignment",
        "audit_disjoint_internal_field_assignment.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "update_field_borrow_provenance_conflict",
        "audit_update_field_borrow_provenance_conflict.r",
        "rejected",
        ("R-EXPR-0013", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "multi_field_assignment_borrow_provenance_uaf",
        "audit_multi_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0002", "R-BORROW-0011"),
    ),
    (
        "branch_field_assignment_borrow_provenance_uaf",
        "audit_branch_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "multi_view_array_reallocation_uaf",
        "audit_multi_view_array_reallocation_uaf.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0011", "R-LIB-0019"),
    ),
    (
        "generic_multi_field_assignment_borrow_provenance_uaf",
        "audit_generic_multi_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-TYPE-0032", "R-INIT-0007", "R-OWN-0009", "R-BORROW-0018"),
    ),
    (
        "error_multi_field_assignment_borrow_provenance_uaf",
        "audit_error_multi_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-ERR-0002", "R-INIT-0007", "R-OWN-0009", "R-BORROW-0018"),
    ),
    (
        "multi_field_releases_replaced_origin",
        "audit_multi_field_releases_replaced_origin.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0006", "R-BORROW-0011"),
    ),
    (
        "multi_field_disjoint_owner_replacement",
        "audit_multi_field_disjoint_owner_replacement.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0006", "R-BORROW-0011"),
    ),
    (
        "move_self_contained_borrow_uaf",
        "audit_move_self_contained_borrow_uaf.r",
        "rejected",
        ("R-OWN-0002", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "generic_move_self_contained_borrow_uaf",
        "audit_generic_move_self_contained_borrow_uaf.r",
        "rejected",
        ("R-TYPE-0032", "R-OWN-0002", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "nested_aggregate_move_self_contained_borrow_uaf",
        "audit_nested_aggregate_move_self_contained_borrow_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0002", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "field_move_self_contained_borrow_uaf",
        "audit_field_move_self_contained_borrow_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0002", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "assign_move_self_contained_borrow_uaf",
        "audit_assign_move_self_contained_borrow_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0002", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "conditional_move_self_contained_borrow_uaf",
        "audit_conditional_move_self_contained_borrow_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0002", "R-BORROW-0002", "R-BORROW-0018"),
    ),
    (
        "borrowed_view_field_storage_reassignment",
        "audit_borrowed_view_field_storage_reassignment.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0011", "R-BORROW-0018"),
    ),
    (
        "move_self_contained_borrow_after_last_use",
        "audit_move_self_contained_borrow_after_last_use.r",
        "accepted",
        ("R-OWN-0002", "R-BORROW-0006", "R-BORROW-0018"),
    ),
    (
        "nested_aggregate_move_after_last_use",
        "audit_nested_aggregate_move_after_last_use.r",
        "accepted",
        ("R-OWN-0002", "R-BORROW-0006", "R-BORROW-0018"),
    ),
    (
        "nested_aggregate_disjoint_owner_replacement",
        "audit_nested_aggregate_disjoint_owner_replacement.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0006", "R-BORROW-0018"),
    ),
    (
        "copy_borrow_aggregate_field_reassignment",
        "audit_copy_borrow_aggregate_field_reassignment.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0011", "R-BORROW-0018"),
    ),
    (
        "copy_borrow_aggregate_dead_copy_reassignment",
        "audit_copy_borrow_aggregate_dead_copy_reassignment.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0006", "R-BORROW-0018"),
    ),
    (
        "nll_future_overwrite_false_rejection",
        "audit_nll_future_overwrite_false_rejection.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0006"),
    ),
    (
        "nll_overwrite_before_source_mutation",
        "audit_nll_overwrite_before_source_mutation.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0006"),
    ),
    (
        "nll_throw_branch_replaces_borrow",
        "audit_nll_throw_branch_replaces_borrow.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0006", "R-ERR-0002"),
    ),
    (
        "nll_break_branch_replaces_borrow",
        "audit_nll_break_branch_replaces_borrow.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0006", "R-STMT-0004", "R-STMT-0009"),
    ),
    (
        "nll_continue_branch_replaces_borrow",
        "audit_nll_continue_branch_replaces_borrow.r",
        "accepted",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0006", "R-STMT-0004", "R-STMT-0009"),
    ),
    (
        "nll_throw_branch_finally_uses_old_borrow",
        "audit_nll_throw_branch_finally_uses_old_borrow.r",
        "rejected",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0006", "R-ERR-0003"),
    ),
    (
        "nll_break_target_uses_old_borrow",
        "audit_nll_break_target_uses_old_borrow.r",
        "rejected",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0006", "R-STMT-0004"),
    ),
    (
        "nll_continue_backedge_uses_old_borrow",
        "audit_nll_continue_backedge_uses_old_borrow.r",
        "rejected",
        ("R-INIT-0007", "R-BORROW-0002", "R-BORROW-0006", "R-STMT-0004"),
    ),
    (
        "switch_nested_break_false_rejection",
        "audit_switch_nested_break_false_rejection.r",
        "accepted",
        ("R-STMT-0004", "R-STMT-0007", "R-STMT-0009"),
    ),
    (
        "switch_break_escaping_finally",
        "audit_switch_break_escaping_finally.r",
        "rejected",
        ("R-STMT-0004", "R-STMT-0009", "R-ERR-0003"),
    ),
    (
        "switch_break_finally_drops_owner",
        "audit_switch_break_finally_drops_owner.r",
        "rejected",
        ("R-OWN-0004", "R-STMT-0009", "R-ERR-0003"),
    ),
    (
        "loop_break_finally_drops_owner",
        "audit_loop_break_finally_drops_owner.r",
        "rejected",
        ("R-OWN-0004", "R-STMT-0009", "R-ERR-0003"),
    ),
    (
        "loop_continue_finally_drops_owner",
        "audit_loop_continue_finally_drops_owner.r",
        "rejected",
        ("R-OWN-0004", "R-STMT-0009", "R-ERR-0003"),
    ),
    (
        "throw_finally_drops_owner_before_catch",
        "audit_throw_finally_drops_owner_before_catch.r",
        "rejected",
        ("R-OWN-0004", "R-ERR-0002", "R-ERR-0003"),
    ),
    (
        "nested_finally_drops_borrow_origin",
        "audit_nested_finally_drops_borrow_origin.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0014", "R-ERR-0003"),
    ),
    (
        "async_nested_finally_drops_borrow_origin",
        "audit_async_nested_finally_drops_borrow_origin.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0014", "R-FUNC-0011", "R-ERR-0003"),
    ),
    (
        "nested_finally_rebinds_borrow_origin",
        "audit_nested_finally_rebinds_borrow_origin.r",
        "rejected",
        ("R-BORROW-0002", "R-BORROW-0014", "R-ERR-0003"),
    ),
    (
        "nested_finally_clears_nullable_owner",
        "audit_nested_finally_clears_nullable_owner.r",
        "rejected",
        ("R-BORROW-0005", "R-OWN-0004", "R-ERR-0003"),
    ),
    (
        "return_nested_finally_reinitializes_owner",
        "audit_return_nested_finally_reinitializes_owner.r",
        "accepted",
        ("R-OWN-0004", "R-OWN-0006", "R-ERR-0003"),
    ),
    (
        "reinitialize_owner_before_nested_finally_control",
        "audit_reinitialize_owner_before_nested_finally_control.r",
        "accepted",
        ("R-OWN-0004", "R-OWN-0006", "R-ERR-0003"),
    ),
    (
        "nll_throw_else_branch_replaces_borrow",
        "audit_nll_throw_else_branch_replaces_borrow.r",
        "accepted",
        ("R-BORROW-0006", "R-ERR-0002"),
    ),
    (
        "nll_infinite_loop_branch_replaces_borrow",
        "audit_nll_infinite_loop_branch_replaces_borrow.r",
        "accepted",
        ("R-BORROW-0006", "R-FUNC-0003"),
    ),
    (
        "nll_terminating_switch_branch_replaces_borrow",
        "audit_nll_terminating_switch_branch_replaces_borrow.r",
        "accepted",
        ("R-BORROW-0006", "R-STMT-0006", "R-ERR-0002"),
    ),
    (
        "nll_explicit_throw_branches_control",
        "audit_nll_explicit_throw_branches_control.r",
        "accepted",
        ("R-BORROW-0006", "R-ERR-0002"),
    ),
    (
        "finally_transfer_keeps_owner_control",
        "audit_finally_transfer_keeps_owner_control.r",
        "accepted",
        ("R-OWN-0004", "R-STMT-0009", "R-ERR-0003"),
    ),
    (
        "atomic_live_assignment",
        "audit_atomic_live_assignment.r",
        "rejected",
        ("R-INIT-0012", "R-INIT-0013"),
    ),
    (
        "atomic_moved_reinitialization",
        "audit_atomic_moved_reinitialization.r",
        "accepted",
        ("R-AM-0006", "R-INIT-0012", "R-INIT-0013"),
    ),
)

HIR_SAFETY_PROBES = (
    (
        "async_multi_field_assignment_borrow_provenance_uaf",
        "audit_async_multi_field_assignment_borrow_provenance_uaf.r",
        "rejected",
        ("R-INIT-0007", "R-OWN-0009", "R-BORROW-0002", "R-FUNC-0011"),
    ),
)

SANITIZER_PROBES = (
    (
        "alloc_new_error_catch_field",
        "audit_alloc_new_error_catch_field_compiler_uaf.r",
    ),
    (
        "alloc_new_error_parameter_control",
        "audit_alloc_new_error_parameter_control.r",
    ),
    (
        "dict_entry_ref_fields",
        "audit_dict_entry_ref_fields.r",
    ),
)


def compiler_identity(path: Path) -> dict[str, str]:
    return {
        "path": str(path),
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def decode_diagnostics(stderr: str) -> list[object]:
    try:
        decoded = json.loads(stderr)
    except json.JSONDecodeError:
        return [{"message": stderr.strip()}] if stderr.strip() else []
    return decoded if isinstance(decoded, list) else [decoded]


def run_compiler(
    root: Path,
    compiler: Path,
    source: Path,
    emit: str,
    environment: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [
            str(compiler),
            "--profile=hosted-native-async",
            f"--emit={emit}",
            "--all-functions",
            "--diagnostics=json",
            str(source),
        ],
        capture_output=True,
        text=True,
        timeout=30,
        check=False,
        cwd=root,
        env=environment,
    )


def semantic_status(process: subprocess.CompletedProcess[str]) -> str:
    if process.returncode == 0 and process.stdout.strip():
        return "accepted"
    if process.returncode == 1:
        return "rejected"
    return "compiler_failure"


def sanitizer_status(process: subprocess.CompletedProcess[str]) -> str:
    stderr = process.stderr
    signatures = (
        "ERROR: AddressSanitizer",
        "ERROR: LeakSanitizer",
        "UndefinedBehaviorSanitizer",
        "runtime error:",
    )
    if any(signature in stderr for signature in signatures):
        return "sanitizer_failure"
    return semantic_status(process)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    fixtures = root / "tests/fixtures"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiler", type=Path, default=root / "build-debug/r-front")
    parser.add_argument(
        "--sanitized-compiler", type=Path, default=root / "build-sanitize/r-front"
    )
    args = parser.parse_args()
    compiler = args.compiler.resolve()
    sanitized_compiler = args.sanitized_compiler.resolve()
    for path in (compiler, sanitized_compiler):
        if not path.is_file():
            parser.error(f"compiler does not exist: {path}")

    results: list[dict[str, object]] = []
    for name, filename, expected, contracts in SEMANTIC_PROBES:
        source = fixtures / filename
        try:
            process = run_compiler(root, compiler, source, "llvm-ir")
        except subprocess.TimeoutExpired:
            results.append(
                {
                    "name": name,
                    "source": str(source.relative_to(root)),
                    "contracts": contracts,
                    "expected": expected,
                    "actual": "timeout",
                    "passed": False,
                }
            )
            continue
        actual = semantic_status(process)
        results.append(
            {
                "name": name,
                "source": str(source.relative_to(root)),
                "contracts": contracts,
                "expected": expected,
                "actual": actual,
                "returncode": process.returncode,
                "diagnostics": decode_diagnostics(process.stderr),
                "passed": actual == expected,
            }
        )

    for name, filename, expected, contracts in HIR_SAFETY_PROBES:
        source = fixtures / filename
        try:
            process = run_compiler(root, compiler, source, "hir")
        except subprocess.TimeoutExpired:
            results.append(
                {
                    "name": name,
                    "source": str(source.relative_to(root)),
                    "contracts": contracts,
                    "emit": "hir",
                    "expected": expected,
                    "actual": "timeout",
                    "passed": False,
                }
            )
            continue
        actual = semantic_status(process)
        results.append(
            {
                "name": name,
                "source": str(source.relative_to(root)),
                "contracts": contracts,
                "emit": "hir",
                "expected": expected,
                "actual": actual,
                "returncode": process.returncode,
                "diagnostics": decode_diagnostics(process.stderr),
                "passed": actual == expected,
            }
        )

    sanitizer_environment = os.environ.copy()
    sanitizer_environment["ASAN_OPTIONS"] = "abort_on_error=1:symbolize=0"
    sanitizer_environment["UBSAN_OPTIONS"] = "halt_on_error=1:print_stacktrace=1"
    for name, filename in SANITIZER_PROBES:
        source = fixtures / filename
        try:
            process = run_compiler(
                root, sanitized_compiler, source, "hir", sanitizer_environment
            )
        except subprocess.TimeoutExpired:
            results.append(
                {
                    "name": name,
                    "source": str(source.relative_to(root)),
                    "expected": "accepted",
                    "actual": "timeout",
                    "passed": False,
                }
            )
            continue
        actual = sanitizer_status(process)
        stderr_lines = process.stderr.strip().splitlines()
        results.append(
            {
                "name": name,
                "source": str(source.relative_to(root)),
                "expected": "accepted",
                "actual": actual,
                "returncode": process.returncode,
                "stderr_excerpt": stderr_lines[:80],
                "passed": actual == "accepted",
            }
        )

    failures = sum(not bool(result["passed"]) for result in results)
    print(
        json.dumps(
            {
                "scope": "bounded ownership and compiler sanitizer regressions",
                "compiler": compiler_identity(compiler),
                "sanitized_compiler": compiler_identity(sanitized_compiler),
                "probes": len(results),
                "passed": len(results) - failures,
                "failed": failures,
                "results": results,
            },
            indent=2,
        )
    )
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
