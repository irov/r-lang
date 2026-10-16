/* Library R-LIB-0028 and Core R-UNSAFE-0008 (B11): the machine operations of core. Each call
   lowers to one R_HIR_STANDARD_CALL: a fence takes its core::memory_order operand, a prefetch its
   raw address (auxiliary type: the pointee), and the others nothing. A fence with a constant
   relaxed order is rejected here (R-MEM-0007); a run-time order is checked where the fence is
   lowered. `core::trap` and `core::unreachable` are never calls, `core::unreachable` needs an
   unsafe context, and the address reads give usize: R has no conversion of an integer back to a
   pointer (R-IDB-019), so the numbers only measure (compiler/README.md, *Machine operations*).
   B12: `core::pointer_address` gives the address of any raw pointer as usize (auxiliary type: the
   pointee), and `unsafe core::offset_bytes` a raw void pointer count bytes from its operand
   (usize forward, isize either way) with the operand's permission and nullability
   (R-SAFETY-OFFSET), which converts to any object pointer as `void *` does in C.
   `core::c_errno` reads and `core::set_c_errno` writes the C errno of the calling thread as a
   c_int. */

static bool r_body_lower_core_machine(RBodyContext *body,
                                      RAstRef argument_list,
                                      const RAstNodeView *arguments_view,
                                      RSourceSpan call_span,
                                      RSourceSpan operation_span,
                                      RStandardCallOperation operation,
                                      RExpressionResult *result) {
    const char *name = r_standard_core_machine_name(operation);
    const uint32_t operand_count = r_standard_core_machine_operand_count(operation);
    const bool fence = (operation == R_STANDARD_CALL_CORE_ATOMIC_FENCE) ||
                       (operation == R_STANDARD_CALL_CORE_SIGNAL_FENCE);
    const bool prefetch = (operation == R_STANDARD_CALL_CORE_PREFETCH_READ) ||
                          (operation == R_STANDARD_CALL_CORE_PREFETCH_WRITE);
    const bool address = operation == R_STANDARD_CALL_CORE_POINTER_ADDRESS;
    const bool offset = operation == R_STANDARD_CALL_CORE_OFFSET_BYTES;
    const bool cast = operation == R_STANDARD_CALL_CORE_CAST_MUTABLE;
    const bool errno_read = operation == R_STANDARD_CALL_CORE_C_ERRNO;
    const bool errno_write = operation == R_STANDARD_CALL_CORE_SET_C_ERRNO;
    RExpressionResult operand;
    RExpressionResult count;
    RHirNodeId child_storage[2];
    RHirVector children;
    RTypeId result_type = R_TYPE_ID_INVALID;
    RTypeId auxiliary_type = R_TYPE_ID_INVALID;
    RHirNodeId node_id;
    RHirNode *node;

    r_body_expression_invalid(result);
    r_body_expression_invalid(&operand);
    r_body_expression_invalid(&count);
    if (name == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if (arguments_view->child_count != operand_count) {
        return r_body_diagnostic(
            body,
            "R-DIAG-TYPE-001",
            ((operation == R_STANDARD_CALL_CORE_UNREACHABLE) || offset || cast) ? "R-UNSAFE-0008"
                                                                                : "R-LIB-0028",
            fence         ? "a core fence takes one core::memory_order"
            : prefetch    ? "a core prefetch takes one raw pointer"
            : address     ? "core::pointer_address takes one raw pointer"
            : offset      ? "core::offset_bytes takes a raw pointer and a usize or isize byte count"
            : cast        ? "core::cast_mutable takes one raw const pointer"
            : errno_write ? "core::set_c_errno takes one c_int"
                          : "this core machine operation takes no argument",
            call_span);
    }
    if ((offset || cast) && (body->unsafe_depth == UINT32_C(0)) &&
        !r_body_diagnostic(body,
                           "R-DIAG-UNSAFE-001",
                           "R-UNSAFE-0001",
                           offset ? "core::offset_bytes is outside an unsafe context"
                                  : "core::cast_mutable is outside an unsafe context",
                           operation_span)) {
        return false;
    }
    if ((operation == R_STANDARD_CALL_CORE_UNREACHABLE) && (body->unsafe_depth == UINT32_C(0)) &&
        !r_body_diagnostic(body,
                           "R-DIAG-UNSAFE-001",
                           "R-UNSAFE-0001",
                           "core::unreachable is outside an unsafe context",
                           operation_span)) {
        return false;
    }
    if (fence) {
        const RSemanticAggregate *memory_order;
        RAstRef order_ref;
        const RHirNode *order_node;

        if (!r_semantic_prepare_core_memory_order_schema(body->frontend, &memory_order) ||
            !r_ast_ref_child(body->frontend, argument_list, UINT32_C(0), &order_ref) ||
            !r_body_lower_value(body, order_ref, memory_order->type, &operand)) {
            return false;
        }
        if (!operand.valid) {
            return true;
        }
        if (r_semantic_value_type(body->frontend, operand.type) != memory_order->type) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-LIB-0028",
                                     "a core fence order shall have type core::memory_order",
                                     call_span);
        }
        /* R-MEM-0007: a fence orders nothing with relaxed (enumerator 0). */
        order_node = r_body_hir_node(body, operand.node);
        if ((order_node != NULL) && (order_node->kind == R_HIR_ENUM_CONSTANT) &&
            (order_node->integer_value == UINT64_C(0))) {
            return r_body_diagnostic(body,
                                     "R-DIAG-ATOMIC-001",
                                     "R-MEM-0007",
                                     "a fence requires acquire, release, acq_rel or seq_cst",
                                     order_node->span);
        }
        auxiliary_type = memory_order->type;
    } else if (prefetch) {
        RAstRef address_ref;
        const RSemanticType *pointer_type;
        const bool write = operation == R_STANDARD_CALL_CORE_PREFETCH_WRITE;

        if (!r_ast_ref_child(body->frontend, argument_list, UINT32_C(0), &address_ref) ||
            !r_body_lower_value(body, address_ref, R_TYPE_ID_INVALID, &operand)) {
            return false;
        }
        if (!operand.valid) {
            return true;
        }
        pointer_type =
            r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, operand.type));
        if ((pointer_type == NULL) || (pointer_type->kind != R_SEMANTIC_TYPE_RAW) ||
            (write && ((pointer_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) != 0U))) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-LIB-0028",
                                     write ? "core::prefetch_write requires raw T*"
                                           : "core::prefetch_read requires a raw pointer",
                                     call_span);
        }
        auxiliary_type = pointer_type->base;
    } else if (cast) {
        RAstRef pointer_ref;
        const RSemanticType *pointer_type;

        if (!r_ast_ref_child(body->frontend, argument_list, UINT32_C(0), &pointer_ref) ||
            !r_body_lower_value(body, pointer_ref, R_TYPE_ID_INVALID, &operand)) {
            return false;
        }
        if (!operand.valid) {
            return true;
        }
        pointer_type =
            r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, operand.type));
        if ((pointer_type == NULL) || (pointer_type->kind != R_SEMANTIC_TYPE_RAW) ||
            ((pointer_type->flags & R_SEMANTIC_TYPE_FLAG_SHARED) == 0U)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-UNSAFE-0008",
                                     "core::cast_mutable requires a raw const pointer",
                                     call_span);
        }
        auxiliary_type = pointer_type->base;
        if (!r_semantic_intern_derived_type(body->frontend,
                                            R_SEMANTIC_TYPE_RAW,
                                            pointer_type->base,
                                            R_TYPE_ID_INVALID,
                                            UINT64_C(0),
                                            pointer_type->flags & R_SEMANTIC_TYPE_FLAG_NULLABLE,
                                            &result_type)) {
            return false;
        }
    } else if (errno_write) {
        RAstRef value_ref;
        RTypeId int_type;

        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_C_INT, &int_type) ||
            !r_ast_ref_child(body->frontend, argument_list, UINT32_C(0), &value_ref) ||
            !r_body_lower_value(body, value_ref, int_type, &operand)) {
            return false;
        }
        if (!operand.valid) {
            return true;
        }
        if (r_semantic_value_type(body->frontend, operand.type) != int_type) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-LIB-0028",
                                     "core::set_c_errno takes one c_int",
                                     call_span);
        }
        auxiliary_type = int_type;
    } else if (address || offset) {
        RAstRef pointer_ref;
        const RSemanticType *pointer_type;

        if (!r_ast_ref_child(body->frontend, argument_list, UINT32_C(0), &pointer_ref) ||
            !r_body_lower_value(body, pointer_ref, R_TYPE_ID_INVALID, &operand)) {
            return false;
        }
        if (!operand.valid) {
            return true;
        }
        pointer_type =
            r_semantic_type(body->frontend, r_semantic_value_type(body->frontend, operand.type));
        if ((pointer_type == NULL) || (pointer_type->kind != R_SEMANTIC_TYPE_RAW)) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     address ? "R-LIB-0028" : "R-UNSAFE-0008",
                                     address ? "core::pointer_address requires a raw pointer"
                                             : "core::offset_bytes requires a raw pointer",
                                     call_span);
        }
        auxiliary_type = pointer_type->base;
        if (offset) {
            RAstRef count_ref;
            RTypeId usize_type;
            RTypeId isize_type;
            RTypeId count_type;
            RTypeId void_type;
            const uint32_t flags =
                pointer_type->flags & (R_SEMANTIC_TYPE_FLAG_SHARED | R_SEMANTIC_TYPE_FLAG_NULLABLE);

            if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &usize_type) ||
                !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_ISIZE, &isize_type) ||
                !r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &void_type) ||
                !r_ast_ref_child(body->frontend, argument_list, UINT32_C(1), &count_ref) ||
                !r_body_lower_value(body, count_ref, R_TYPE_ID_INVALID, &count)) {
                return false;
            }
            if (!count.valid) {
                return true;
            }
            count_type = r_semantic_value_type(body->frontend, count.type);
            if ((count_type != usize_type) && (count_type != isize_type)) {
                return r_body_diagnostic(body,
                                         "R-DIAG-TYPE-001",
                                         "R-UNSAFE-0008",
                                         "the byte count of core::offset_bytes shall be usize or "
                                         "isize",
                                         call_span);
            }
            if (!r_semantic_intern_derived_type(body->frontend,
                                                R_SEMANTIC_TYPE_RAW,
                                                void_type,
                                                R_TYPE_ID_INVALID,
                                                UINT64_C(0),
                                                flags,
                                                &result_type)) {
                return false;
            }
        }
    }
    if (offset || cast) {
        /* The result pointer type is set above. */
    } else if (fence || prefetch || errno_write) {
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_VOID, &result_type)) {
            return false;
        }
    } else if (errno_read) {
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_C_INT, &result_type)) {
            return false;
        }
    } else if ((operation == R_STANDARD_CALL_CORE_TRAP) ||
               (operation == R_STANDARD_CALL_CORE_UNREACHABLE)) {
        if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_NEVER, &result_type)) {
            return false;
        }
    } else if (!r_semantic_type_from_token(body->frontend, R_TOKEN_KW_USIZE, &result_type)) {
        return false;
    }
    if (auxiliary_type == R_TYPE_ID_INVALID) {
        auxiliary_type = result_type;
    }
    child_storage[0] = operand.node;
    child_storage[1] = count.node;
    children.items = child_storage;
    children.count = operand_count;
    children.capacity = UINT32_C(2);
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            call_span,
                            result_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &node_id)) {
        return false;
    }
    node = r_body_hir_node(body, node_id);
    if (node == NULL) {
        body->frontend->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    node->standard_operation = operation;
    node->auxiliary_type = auxiliary_type;
    result->node = node_id;
    result->type = result_type;
    result->valid = true;
    return true;
}
