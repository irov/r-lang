/* R-AGG-0012 (L27): the `@derive` attribute of a struct, enum or error declaration. The parser
 * appends the declarations that implement equal, ordered, key and format
 * (compiler/parser/derive.inc);
 * this pass records the capabilities, makes `clone` structural and checks that every field and
 * payload proves the derived capability, reporting the first member that does not. */

enum {
    R_SEMANTIC_DERIVE_CLONE = 1U << 0U,
    R_SEMANTIC_DERIVE_EQUAL = 1U << 1U,
    R_SEMANTIC_DERIVE_ORDERED = 1U << 2U,
    R_SEMANTIC_DERIVE_KEY = 1U << 3U,
    R_SEMANTIC_DERIVE_FORMAT = 1U << 4U,
};

enum { R_SEMANTIC_DERIVE_COUNT = 5 };

static const char *const r_semantic_derive_names[R_SEMANTIC_DERIVE_COUNT] = {
    "clone", "equal", "ordered", "key", "format"};

static bool r_semantic_derive_attribute(RFrontendContext *context,
                                        RAstRef attribute,
                                        const RAstNodeView *view,
                                        bool in_extern,
                                        RSemanticAggregate *aggregate) {
    uint32_t derived = 0U;
    RSourceSpan origins[R_SEMANTIC_DERIVE_COUNT];
    (void)memset(origins, 0, sizeof(origins));
    if (in_extern) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-SYN-002",
                                         "R-AGG-0012",
                                         "@derive is not available for a C aggregate",
                                         view->span);
    }
    if (aggregate->derived != 0U) {
        return r_semantic_add_diagnostic(context,
                                         "R-DIAG-SYN-002",
                                         "R-AGG-0012",
                                         "a declaration has at most one @derive attribute",
                                         view->span);
    }
    for (uint32_t index = 2U; index < view->child_count; ++index) {
        RAstRef argument;
        RAstNodeView argument_view;
        RAstTokenView name;
        uint32_t bit = R_SEMANTIC_DERIVE_COUNT;
        if (!r_ast_ref_child(context, attribute, index, &argument) ||
            !r_ast_ref_view(context, argument, &argument_view)) {
            continue;
        }
        if ((argument_view.kind == R_SYNTAX_TYPE) && (argument_view.child_count == 1U) &&
            r_ast_direct_token(context, argument, UINT32_C(0), NULL, &name) &&
            (name.kind == R_TOKEN_IDENTIFIER)) {
            for (bit = 0U; bit < R_SEMANTIC_DERIVE_COUNT; ++bit) {
                if (r_semantic_token_text_equal_owned(
                        context, &name, r_semantic_derive_names[bit])) {
                    break;
                }
            }
        }
        if (bit == R_SEMANTIC_DERIVE_COUNT) {
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-SYN-002",
                                           "R-AGG-0012",
                                           "@derive lists only clone, equal, ordered, key and "
                                           "format",
                                           argument_view.span)) {
                return false;
            }
            aggregate->poisoned = true;
            continue;
        }
        if ((derived & (1U << bit)) != 0U) {
            if (!r_semantic_add_diagnostic(context,
                                           "R-DIAG-SYN-002",
                                           "R-AGG-0012",
                                           "@derive lists each capability at most once",
                                           argument_view.span)) {
                return false;
            }
            continue;
        }
        derived |= 1U << bit;
        origins[bit] = argument_view.span;
    }
    if (derived == 0U) {
        if (view->child_count <= 2U) {
            aggregate->poisoned = true;
            return r_semantic_add_diagnostic(context,
                                             "R-DIAG-SYN-002",
                                             "R-AGG-0012",
                                             "@derive lists at least one capability",
                                             view->span);
        }
        return true;
    }
    aggregate->derived = derived;
    (void)memcpy(aggregate->derive_origins, origins, sizeof(origins));
    aggregate->clone_derived = (derived & R_SEMANTIC_DERIVE_CLONE) != 0U;
    return true;
}

/* The std.cmp trait `name`, or 0 when std.cmp is not part of the program. */
static uint32_t r_derive_standard_trait(const RFrontendContext *context, const char *name) {
    for (size_t index = 0U; index < context->semantic_trait_count; ++index) {
        const RSemanticTrait *trait = &context->semantic_traits[index];
        const RSource *declaration = r_get_source_const(context, trait->module_source);
        if ((trait->generic_origin != 0U) || trait->is_core || (declaration == NULL) ||
            (declaration->module_name == NULL) ||
            (strcmp(declaration->module_name, "std.cmp") != 0) ||
            !r_source_text_equal(declaration, trait->name_span, name)) {
            continue;
        }
        return (uint32_t)index + 1U;
    }
    return 0U;
}

/* The derivation region generated for the capability named at `origin`. */
static RDeriveRegion *r_derive_region_of(RFrontendContext *context, RSourceSpan origin) {
    RSource *source = r_get_source(context, origin.source);
    if (source == NULL) {
        return NULL;
    }
    for (size_t index = 0U; index < source->derive_region_count; ++index) {
        RDeriveRegion *region = &source->derive_regions[index];
        if ((region->origin.start == origin.start) && (region->origin.end == origin.end)) {
            return region;
        }
    }
    return NULL;
}

/* Whether the member type `type` proves the derived capability `bit`; a type that depends on
   the parameters is proven by the constraints the derivation adds to them. A member of a type
   that is not Copy is cloned only when it holds no borrow (R-OWN-0020). */
static bool r_derive_member_proves(RFrontendContext *context,
                                   RTypeId type,
                                   uint32_t bit,
                                   uint32_t equal,
                                   uint32_t ordered,
                                   bool copy) {
    if ((type == R_TYPE_ID_INVALID) || r_generic_type_is_dependent(context, type, 0U)) {
        return true;
    }
    switch (bit) {
    case R_SEMANTIC_DERIVE_CLONE:
        return r_clone_capability_visit(context, type, 0U, NULL) &&
               (copy || r_generic_has_property(context, type, R_GENERIC_UNBORROWED, 0U));
    case R_SEMANTIC_DERIVE_EQUAL:
        return (equal != 0U) && r_semantic_type_implements_trait(context, type, equal);
    case R_SEMANTIC_DERIVE_ORDERED:
        return (ordered != 0U) && r_semantic_type_implements_trait(context, type, ordered);
    case R_SEMANTIC_DERIVE_FORMAT:
        return r_semantic_declare_core_traits(context) &&
               r_semantic_type_implements_trait(context, type, context->core_format_trait);
    default:
        return r_generic_has_property(context, type, R_GENERIC_KEY, 0U);
    }
}

/* Reports that the member `member` (a field or variant name, `variant.field` for a field of a
   record variant) does not prove capability `bit`. */
static bool r_derive_report_member(RFrontendContext *context,
                                   const RSemanticAggregate *aggregate,
                                   uint32_t slot,
                                   uint32_t variant_name,
                                   uint32_t member_name) {
    static const char *const proofs[R_SEMANTIC_DERIVE_COUNT] = {
        "be cloneable without holding a borrow (R-OWN-0020)",
        "implement std.cmp::Equal",
        "implement std.cmp::Ordered",
        "prove key (R-FUNC-0009)",
        "satisfy core::Format",
    };
    char text[320];
    const char *variant_text = "";
    size_t variant_length = 0U;
    const char *member_text = "";
    size_t member_length = 0U;
    uint32_t message = 0U;
    int written;
    if (variant_name != 0U) {
        (void)r_semantic_intern_text(context, variant_name, &variant_text, &variant_length);
    }
    if (member_name != 0U) {
        (void)r_semantic_intern_text(context, member_name, &member_text, &member_length);
    }
    written = snprintf(text,
                       sizeof(text),
                       "derived %s requires every field and payload to %s; `%.*s%s%.*s` does "
                       "not",
                       r_semantic_derive_names[slot],
                       proofs[slot],
                       (int)variant_length,
                       variant_text,
                       (variant_length != 0U && member_length != 0U) ? "." : "",
                       (int)member_length,
                       member_text);
    if (written < 0) {
        context->resource_status = R_FRONTEND_INTERNAL_ERROR;
        return false;
    }
    if ((size_t)written >= sizeof(text)) {
        written = (int)(sizeof(text) - 1U);
    }
    if (!r_intern_bytes(context, (const uint8_t *)text, (size_t)written, &message)) {
        return false;
    }
    return r_semantic_add_diagnostic(context,
                                     "R-DIAG-TRAIT-001",
                                     "R-AGG-0012",
                                     (const char *)context->intern_entries[message - 1U].bytes,
                                     aggregate->derive_origins[slot]);
}

/* The first member of `aggregate` that does not prove capability `bit`, reported; true when
   every member proves it or after the report. `failed` tells which. */
static bool r_derive_check_members(RFrontendContext *context,
                                   const RSemanticAggregate *aggregate,
                                   uint32_t slot,
                                   uint32_t equal,
                                   uint32_t ordered,
                                   bool *failed) {
    const uint32_t bit = 1U << slot;
    const bool copy = r_semantic_type_is_copy(context, aggregate->type);
    *failed = false;
    /* The fields of an enum are the storage of its payloads; its variants are checked below. */
    for (uint32_t index = 0U;
         (aggregate->kind != R_SEMANTIC_AGGREGATE_ENUM) && (index < aggregate->field_count);
         ++index) {
        const RSemanticField field = context->semantic_fields[aggregate->first_field + index];
        if (!r_derive_member_proves(context, field.type, bit, equal, ordered, copy)) {
            *failed = true;
            return r_derive_report_member(context, aggregate, slot, 0U, field.name_intern_id);
        }
    }
    for (uint32_t index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant variant =
            context->semantic_variants[aggregate->first_variant + index];
        const RSemanticAggregate *record =
            variant.structured_payload
                ? r_semantic_aggregate_for_type(context, variant.payload_type)
                : NULL;
        if (record != NULL) {
            for (uint32_t field = 0U; field < record->field_count; ++field) {
                const RSemanticField member =
                    context->semantic_fields[record->first_field + field];
                if (!r_derive_member_proves(context, member.type, bit, equal, ordered, copy)) {
                    *failed = true;
                    return r_derive_report_member(
                        context, aggregate, slot, variant.name_intern_id, member.name_intern_id);
                }
            }
        } else if ((variant.payload_type != R_TYPE_ID_INVALID) &&
                   !r_derive_member_proves(
                       context, variant.payload_type, bit, equal, ordered, copy)) {
            *failed = true;
            return r_derive_report_member(context, aggregate, slot, variant.name_intern_id, 0U);
        }
    }
    return true;
}

/* R-AGG-0011, R-AGG-0012: a value of the type of an error with descendants holds any member
   of its family, so such an error does not derive; its members may. Reported before the
   declarations derived for it are checked, which then add nothing. */
static bool r_derive_reject_families(RFrontendContext *context) {
    for (size_t index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate aggregate = context->semantic_aggregates[index];
        uint32_t first = 0U;
        if ((aggregate.derived == 0U) || !aggregate.error_has_children) {
            continue;
        }
        while ((aggregate.derived & (1U << first)) == 0U) {
            ++first;
        }
        for (uint32_t slot = 0U; slot < R_SEMANTIC_DERIVE_COUNT; ++slot) {
            RDeriveRegion *region =
                (aggregate.derived & (1U << slot)) != 0U
                    ? r_derive_region_of(context, aggregate.derive_origins[slot])
                    : NULL;
            if (region != NULL) {
                region->suppressed = true;
            }
        }
        if (!r_semantic_add_diagnostic(context,
                                       "R-DIAG-TYPE-001",
                                       "R-AGG-0012",
                                       "an error with descendants holds any member of its family "
                                       "and does not derive; its members may",
                                       aggregate.derive_origins[first])) {
            return false;
        }
    }
    return true;
}

static bool r_derive_validate(RFrontendContext *context) {
    const uint32_t equal = r_derive_standard_trait(context, "Equal");
    const uint32_t ordered = r_derive_standard_trait(context, "Ordered");
    for (size_t index = 0U; index < context->semantic_aggregate_count; ++index) {
        const RSemanticAggregate aggregate = context->semantic_aggregates[index];
        if ((aggregate.derived == 0U) || (aggregate.generic_origin != 0U) || aggregate.poisoned ||
            !aggregate.complete) {
            continue;
        }
        if (aggregate.error_has_children) {
            continue;
        }
        for (uint32_t slot = 0U; slot < R_SEMANTIC_DERIVE_COUNT; ++slot) {
            const uint32_t bit = 1U << slot;
            RDeriveRegion *region = NULL;
            bool failed = false;
            if ((aggregate.derived & bit) == 0U) {
                continue;
            }
            if (bit != R_SEMANTIC_DERIVE_CLONE) {
                region = r_derive_region_of(context, aggregate.derive_origins[slot]);
                if ((region != NULL) && region->incomplete) {
                    /* The parent is unresolved and reported, or its fields were unreadable. */
                    if ((aggregate.error_parent != 0U) &&
                        !r_semantic_add_diagnostic(context,
                                                   "R-DIAG-TYPE-001",
                                                   "R-AGG-0012",
                                                   "@derive requires the declaration of the parent "
                                                   "error to be readable",
                                                   aggregate.derive_origins[slot])) {
                        return false;
                    }
                    continue;
                }
                if (region == NULL) {
                    if (!r_semantic_add_diagnostic(
                            context,
                            "R-DIAG-SYN-002",
                            "R-AGG-0012",
                            "@derive of equal, ordered, key or format requires a module-scope "
                            "declaration outside static conditions",
                            aggregate.derive_origins[slot])) {
                        return false;
                    }
                    continue;
                }
            } else if (aggregate.clone_function != 0U) {
                if (!r_semantic_add_diagnostic(
                        context,
                        "R-DIAG-NAME-002",
                        "R-AGG-0012",
                        "a type with a clone hook does not also derive clone",
                        aggregate.derive_origins[slot])) {
                    return false;
                }
                continue;
            }
            if (!r_derive_check_members(context, &aggregate, slot, equal, ordered, &failed)) {
                return false;
            }
            if (failed && (region != NULL)) {
                region->suppressed = true;
            }
        }
    }
    return true;
}
