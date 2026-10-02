/* Each helper has ordinary, concrete R parameter and checked-result types. */
static const RHirNode *
r_c17_format_parameter(RC17Emitter *emitter, const RHirNode *function, uint32_t index) {
    return r_c17_node(emitter, r_c17_child(emitter, function, index));
}

static bool r_c17_format_argument(RC17Emitter *emitter, const RHirNode *function, uint32_t index) {
    const RHirNode *parameter = r_c17_format_parameter(emitter, function, index);
    return parameter != NULL && r_c17_emit_variable_name(emitter, parameter->symbol);
}

static bool r_c17_format_capture(RC17Emitter *emitter,
                                 const RHirNode *function,
                                 const RFormatRecipe *recipe,
                                 uint32_t index) {
    if (recipe->kind == R_FORMAT_RENDER) {
        if (!r_c17_format_argument(emitter, function, 0U) || !r_c17_write(emitter, "->")) {
            return false;
        }
    } else if (!r_c17_write(emitter, "r_captures.")) {
        return false;
    }
    return r_c17_format(emitter, "r_m%08" PRIu32, index + 1U);
}

static bool r_c17_format_value(RC17Emitter *emitter,
                               const RHirNode *function,
                               const RFormatRecipe *recipe,
                               const RFormatSchema *schema,
                               const RFormatPart *part) {
    return part->kind == R_FORMAT_CAPTURE
               ? r_c17_format_capture(emitter, function, recipe, part->index)
               : r_c17_format_argument(
                     emitter,
                     function,
                     (recipe->kind == R_FORMAT_RENDER ? 1U : schema->capture_count) + part->index);
}

static RTypeId r_c17_format_part_type(RC17Emitter *emitter,
                                      const RHirNode *function,
                                      const RFormatRecipe *recipe,
                                      const RFormatSchema *schema,
                                      const RFormatPart *part) {
    if (part->kind == R_FORMAT_CAPTURE) {
        const RSemanticType *type = r_c17_type(emitter, schema->type);
        const RSemanticAggregate *aggregate =
            &emitter->frontend->semantic_aggregates[type->base - 1U];
        return emitter->frontend->semantic_fields[aggregate->first_field + part->index].type;
    }
    return r_c17_format_parameter(emitter,
                                  function,
                                  (recipe->kind == R_FORMAT_RENDER ? 1U : schema->capture_count) +
                                      part->index)
        ->type;
}

static bool r_c17_format_spec(RC17Emitter *emitter, const RFormatSpec *spec) {
    return r_c17_format(emitter,
                        "        RStdFormatSpec r_spec = {\n"
                        "            .width = UINT64_C(%" PRIu64 "),\n"
                        "            .precision = UINT64_C(%" PRIu64 "),\n"
                        "            .radix = UINT32_C(%" PRIu32 "),\n"
                        "            .uppercase = %u,\n"
                        "            .zero_fill = %u,\n"
                        "            .fixed = %u,\n"
                        "        };\n",
                        spec->width,
                        spec->precision,
                        spec->radix,
                        spec->uppercase ? 1U : 0U,
                        spec->zero_fill ? 1U : 0U,
                        spec->fixed ? 1U : 0U);
}

static bool r_c17_format_append_part(RC17Emitter *emitter,
                                     const RHirNode *function,
                                     const RFormatRecipe *recipe,
                                     const RFormatSchema *schema,
                                     const RFormatPart *part,
                                     uint32_t ordinal) {
    RTypeId type_id = R_TYPE_ID_INVALID;
    const RSemanticType *type = NULL;
    RSemanticTypeKind kind = R_SEMANTIC_TYPE_INVALID;
    RTokenKind c_token = R_TOKEN_INVALID;
    bool floating;
    bool integer;
    if (!r_c17_write(emitter, "    if (r_status.status == R_STD_FORMAT_CALL_SUCCESS) {\n")) {
        return false;
    }
    if (part->kind == R_FORMAT_TEXT) {
        const RInternEntry *text = &emitter->frontend->intern_entries[part->index - 1U];
        if (!r_c17_format(emitter, "        static const uint8_t r_text_%u[] = ", ordinal) ||
            !r_c17_emit_byte_string(emitter, text->bytes, text->length, 2U) ||
            !r_c17_format(emitter,
                          "        RStdStringView r_text = {r_text_%u, %zuU};\n"
                          "        r_status = r_std_format_append_str(&r_builder, r_text);\n"
                          "    }\n",
                          ordinal,
                          text->length)) {
            return false;
        }
        return true;
    }
    type_id = r_c17_format_part_type(emitter, function, recipe, schema, part);
    type = r_c17_type(emitter, type_id);
    kind = type->kind;
    if (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        c_token = (RTokenKind)type->length;
        if (c_token == R_TOKEN_KW_C_BOOL) {
            kind = R_SEMANTIC_TYPE_BOOL;
        }
    }
    floating = kind == R_SEMANTIC_TYPE_F32 || kind == R_SEMANTIC_TYPE_F64 ||
               c_token == R_TOKEN_KW_C_FLOAT || c_token == R_TOKEN_KW_C_DOUBLE ||
               c_token == R_TOKEN_KW_C_LONG_DOUBLE;
    integer = (kind >= R_SEMANTIC_TYPE_I8 && kind <= R_SEMANTIC_TYPE_USIZE) ||
              (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC && !floating);
    if (integer) {
        RSemanticTypeKind representation =
            r_semantic_integer_representation(emitter->frontend, type_id);
        bool negative =
            representation >= R_SEMANTIC_TYPE_I8 && representation <= R_SEMANTIC_TYPE_ISIZE;
        if (!r_c17_write(emitter, "        RStdConvertParsedInteger r_integer = {0};\n")) {
            return false;
        }
        if (negative && (!r_c17_write(emitter, "        r_integer.negative = ") ||
                         !r_c17_format_value(emitter, function, recipe, schema, part) ||
                         !r_c17_write(emitter, " < 0;\n"))) {
            return false;
        }
        if (!r_c17_write(emitter, "        r_integer.magnitude = (uint64_t)") ||
            !r_c17_format_value(emitter, function, recipe, schema, part) ||
            !r_c17_write(emitter, ";\n")) {
            return false;
        }
        if (negative &&
            !r_c17_write(emitter,
                         "        if (r_integer.negative) {\n"
                         "            r_integer.magnitude = UINT64_C(0) - r_integer.magnitude;\n"
                         "        }\n")) {
            return false;
        }
        if (!r_c17_format_spec(emitter, &part->spec) ||
            !r_c17_write(emitter,
                         "        r_status = r_library_internal_format_integer(&r_builder, "
                         "r_integer, r_spec);\n")) {
            return false;
        }
    } else if (floating) {
        uint32_t source_kind = kind == R_SEMANTIC_TYPE_F32      ? 0U
                               : kind == R_SEMANTIC_TYPE_F64    ? 1U
                               : c_token == R_TOKEN_KW_C_FLOAT  ? 2U
                               : c_token == R_TOKEN_KW_C_DOUBLE ? 3U
                                                                : 4U;
        if (!r_c17_format_spec(emitter, &part->spec) ||
            !r_c17_write(emitter,
                         source_kind == 0U || source_kind == 2U
                             ? "        float r_value = "
                             : "        long double r_value = (long double)") ||
            !r_c17_format_value(emitter, function, recipe, schema, part) ||
            !r_c17_format(emitter,
                          ";\n        r_status = %s(&r_builder, "
                          "r_value, UINT32_C(%u), r_spec);\n",
                          source_kind == 0U || source_kind == 2U
                              ? "r_library_internal_format_float32"
                              : "r_library_internal_format_float",
                          source_kind)) {
            return false;
        }
    } else if (kind == R_SEMANTIC_TYPE_CHAR && part->spec.width != 0U) {
        /* R-EXPR-0028 (L32): a width pads text to that many Unicode scalar values. */
        if (!r_c17_format_spec(emitter, &part->spec) ||
            !r_c17_write(emitter, "        r_status = r_library_internal_format_char(&r_builder, ") ||
            !r_c17_format_value(emitter, function, recipe, schema, part) ||
            !r_c17_write(emitter, ", r_spec);\n")) {
            return false;
        }
    } else if (kind == R_SEMANTIC_TYPE_CHAR) {
        if (!r_c17_write(emitter, "        r_status = r_std_format_append_char(&r_builder, ") ||
            !r_c17_format_value(emitter, function, recipe, schema, part) ||
            !r_c17_write(emitter, ");\n")) {
            return false;
        }
    } else {
        if (!r_c17_write(emitter, "        RStdStringView r_text = ")) {
            return false;
        }
        if (kind == R_SEMANTIC_TYPE_BOOL) {
            if (!r_c17_format_value(emitter, function, recipe, schema, part) ||
                !r_c17_write(emitter,
                             "\n                                    ? (RStdStringView){(const "
                             "uint8_t *)\"true\", 4U}\n"
                             "                                    : (RStdStringView){(const "
                             "uint8_t *)\"false\", 5U};\n")) {
                return false;
            }
        } else if (kind == R_SEMANTIC_TYPE_STR || kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) {
            const char *data = kind == R_SEMANTIC_TYPE_STR ? ".data" : ".r_data";
            const char *length = kind == R_SEMANTIC_TYPE_STR ? ".length" : ".r_len";
            if (!r_c17_write(emitter, "{\n            .data = ") ||
                !r_c17_format_value(emitter, function, recipe, schema, part) ||
                !r_c17_format(emitter, "%s,\n            .length = ", data) ||
                !r_c17_format_value(emitter, function, recipe, schema, part) ||
                !r_c17_format(emitter, "%s,\n        };\n", length)) {
                return false;
            }
        } else if (!r_c17_write(emitter, "r_std_string_as_str(") ||
                   (kind != R_SEMANTIC_TYPE_BORROW && !r_c17_write(emitter, "&")) ||
                   !r_c17_format_value(emitter, function, recipe, schema, part) ||
                   !r_c17_write(emitter, ");\n")) {
            return false;
        }
        if (part->spec.width != 0U
                ? (!r_c17_format_spec(emitter, &part->spec) ||
                   !r_c17_write(emitter,
                                "        r_status = r_library_internal_format_text(&r_builder, "
                                "r_text, r_spec);\n"))
                : !r_c17_write(emitter,
                               "        r_status = r_std_format_append_str(&r_builder, r_text);\n")) {
            return false;
        }
    }
    return r_c17_write(emitter, "    }\n");
}

static bool r_c17_emit_format_function(RC17Emitter *emitter, RSymbolId function_id) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_id);
    const RHirNode *node = r_c17_node(emitter, emitter->function_nodes[function_id]);
    const RFormatRecipe *recipe = &emitter->frontend->format_recipes[function->format_recipe - 1U];
    const RFormatSchema *schema =
        recipe->schema == 0U ? NULL : &emitter->frontend->format_schemas[recipe->schema - 1U];
    bool create = recipe->kind == R_FORMAT_CREATE;
    bool captures = recipe->kind == R_FORMAT_CREATE || recipe->kind == R_FORMAT_IMMEDIATE;
    uint32_t index;
    if (!r_c17_emit_function_signature(emitter, function_id, true) ||
        !r_c17_write(emitter, " {\n")) {
        return false;
    }
    for (index = 0U; index < function->parameter_count; ++index) {
        if (!r_c17_write(emitter, "    (void)") || !r_c17_format_argument(emitter, node, index) ||
            !r_c17_write(emitter, ";\n")) {
            return false;
        }
    }
    if (recipe->kind == R_FORMAT_STRING_FROM_STR) {
        if (!r_c17_write(emitter, "    RStdStringView r_source = {\n        .data = ") ||
            !r_c17_format_argument(emitter, node, 0U) ||
            !r_c17_write(emitter, ".data,\n        .length = ") ||
            !r_c17_format_argument(emitter, node, 0U) ||
            !r_c17_write(emitter,
                         ".length,\n    };\n"
                         "    RStdStringAllocValueResult r_copy =\n"
                         "        r_std_string_from_str(r_runtime_hosted_allocator(), r_source);\n"
                         "    if (r_copy.status == R_STD_STRING_CALL_SUCCESS) {\n"
                         "        r_effect_out->r_payload.r_ok = r_copy.value;\n"
                         "        r_effect_out->r_tag = UINT32_C(0);\n"
                         "    } else {\n"
                         "        r_effect_out->r_payload.r_error_00000001 = r_copy.error;\n"
                         "        r_effect_out->r_tag = UINT32_C(1);\n"
                         "    }\n}\n\n")) {
            return false;
        }
        return true;
    }
    if (recipe->kind == R_FORMAT_STRING_AS_STR || recipe->kind == R_FORMAT_STRING_AS_BYTES) {
        if (!r_c17_write(emitter, "    RStdStringView r_view = r_std_string_as_str(") ||
            !r_c17_format_argument(emitter, node, 0U) ||
            !r_c17_write(emitter, ");\n    return (") ||
            !r_c17_emit_value_type(emitter, function->return_type) ||
            !r_c17_write(emitter,
                         recipe->kind == R_FORMAT_STRING_AS_STR
                             ? "){.data = r_view.data, .length = r_view.length};\n}\n\n"
                             : "){.r_data = r_view.data, .r_len = r_view.length};\n}\n\n")) {
            return false;
        }
        return true;
    }
    if (recipe->kind == R_FORMAT_STRING_INTO_BYTES) {
        return r_c17_write(emitter,
                           "    RRuntimeArray r_bytes = {0};\n"
                           "    (void)r_std_string_into_bytes(&") &&
               r_c17_format_argument(emitter, node, 0U) &&
               r_c17_write(emitter, ", &r_bytes);\n    return r_bytes;\n}\n\n");
    }
    if (!r_c17_write(emitter, "    RStdFormatAllocResult r_status = {0};\n")) {
        return false;
    }
    if (captures) {
        if (!r_c17_write(emitter, "    ") || !r_c17_emit_value_type(emitter, schema->type) ||
            !r_c17_write(emitter, " r_captures = {0};\n")) {
            return false;
        }
        for (index = 0U; index < schema->capture_count; ++index) {
            const RHirNode *parameter = r_c17_format_parameter(emitter, node, index);
            const RSemanticType *type = r_c17_type(emitter, parameter->type);
            if (type->kind == R_SEMANTIC_TYPE_BORROW || type->kind == R_SEMANTIC_TYPE_STR) {
                if (!r_c17_write(emitter,
                                 "    if (r_status.status == R_STD_FORMAT_CALL_SUCCESS) {\n"
                                 "        RStdStringView r_view = ")) {
                    return false;
                }
                if (type->kind == R_SEMANTIC_TYPE_BORROW) {
                    if (!r_c17_write(emitter, "r_std_string_as_str(") ||
                        !r_c17_format_argument(emitter, node, index) ||
                        !r_c17_write(emitter, ");\n")) {
                        return false;
                    }
                } else if (!r_c17_write(emitter, "{\n            .data = ") ||
                           !r_c17_format_argument(emitter, node, index) ||
                           !r_c17_write(emitter, ".data,\n            .length = ") ||
                           !r_c17_format_argument(emitter, node, index) ||
                           !r_c17_write(emitter, ".length,\n        };\n")) {
                    return false;
                }
                if (!r_c17_write(
                        emitter,
                        "        RStdStringAllocValueResult r_copy =\n"
                        "            r_std_string_from_str(r_runtime_hosted_allocator(), r_view);\n"
                        "        r_status.status = (RStdFormatCallStatus)r_copy.status;\n"
                        "        r_status.error = r_copy.error;\n"
                        "        if (r_copy.status == R_STD_STRING_CALL_SUCCESS) {\n           "
                        " ") ||
                    !r_c17_format_capture(emitter, node, recipe, index) ||
                    !r_c17_write(emitter, " = r_copy.value;\n        }\n    }\n")) {
                    return false;
                }
            } else if (!r_c17_write(emitter, "    ") ||
                       !r_c17_format_capture(emitter, node, recipe, index) ||
                       !r_c17_write(emitter, " = ") ||
                       !r_c17_format_argument(emitter, node, index) ||
                       !r_c17_write(emitter, ";\n")) {
                return false;
            }
        }
    }
    if (!create) {
        if (!r_c17_write(emitter,
                         "    RStdFormatBuilderResult r_created = "
                         "r_std_format_create(r_runtime_hosted_allocator());\n"
                         "    RStdFormatBuilder r_builder = r_created.value;\n")) {
            return false;
        }
        for (index = 0U; index < schema->part_count; ++index) {
            if (!r_c17_format_append_part(
                    emitter,
                    node,
                    recipe,
                    schema,
                    &emitter->frontend->format_parts[schema->first_part + index],
                    index)) {
                return false;
            }
        }
    }
    if (!r_c17_write(emitter,
                     "    if (r_status.status == R_STD_FORMAT_CALL_SUCCESS) {\n"
                     "        r_effect_out->r_payload.r_ok = ") ||
        !r_c17_write(emitter, create ? "r_captures;\n" : "r_std_format_finish(&r_builder);\n") ||
        !r_c17_write(emitter, "        r_effect_out->r_tag = UINT32_C(0);\n")) {
        return false;
    }
    if (create &&
        (!r_c17_write(emitter, "        r_captures = (") ||
         !r_c17_emit_value_type(emitter, schema->type) || !r_c17_write(emitter, "){0};\n"))) {
        return false;
    }
    if (!r_c17_write(emitter,
                     "    } else {\n"
                     "        r_effect_out->r_payload.r_error_00000001 = r_status.error;\n"
                     "        r_effect_out->r_tag = UINT32_C(1);\n"
                     "    }\n")) {
        return false;
    }
    if (!create && !r_c17_write(emitter, "    r_std_string_destroy(&r_builder.output);\n")) {
        return false;
    }
    if (captures &&
        (!r_c17_write(emitter, "    ") || !r_c17_emit_type_drop_name(emitter, schema->type) ||
         !r_c17_write(emitter, "(&r_captures);\n"))) {
        return false;
    }
    return r_c17_write(emitter, "}\n\n");
}
