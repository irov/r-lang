#include "emit_internal.h"

#include <llvm-c/Support.h>

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Stack bounds of entries (R-FUNC-0004). A stack check before an entry requires the entry's own
   frame plus the deepest path of calls below it through the functions the module defines; the
   runtime and library functions it reaches keep their own budget, as with the C17 emitter
   (tools/compute_stack_entries.py). Frame sizes are measured on the code the target machine
   generates: a copy of the finished module is compiled once while the prologue-epilogue
   inserter reports each function's stack size, the bounds are computed, written into the
   constants the checks read, and the module itself is then compiled for output.

   The optimizer runs before the measurement, so the frames and calls measured are those of the
   optimized code. Three things keep that sound: the bound constants are externally initialized,
   so no load of one is folded before the bound is written; a function whose frame is below a
   callback the runtime or library makes (a stack edge), that holds an indirect call, or that
   carries a @recursion depth is never inlined, so the call that needs its edges or its depth stays
   in its frame; and every function and constant this file refers to after the optimizer is kept
   by llvm.compiler.used, so none of them is deleted under it. */

static bool r_llvm_holds_indirect_calls(LLVMValueRef function);
static bool r_llvm_guarded_call_marked(RLlvmEmitter *emitter, LLVMValueRef call);

/* A function the object defines: an imported available_externally body is never emitted, so a
   call of it is a call of the archive's function, which keeps its own budget (import.c). */
static bool r_llvm_stack_defined(LLVMValueRef function) {
    return (LLVMCountBasicBlocks(function) != 0U) &&
           (LLVMGetLinkage(function) != LLVMAvailableExternallyLinkage);
}

/* The bound of an entry: the step of an async function or an ordinary entry, or with `direct` the
   direct twin of an async function (direct.c). */
static LLVMValueRef r_llvm_stack_bound(RLlvmEmitter *emitter, RSymbolId symbol, bool direct) {
    RLlvmStackEntry *entry = NULL;
    size_t index;

    for (index = 0U; index < emitter->stack_entry_count; ++index) {
        if ((emitter->stack_entries[index].symbol == symbol) &&
            (emitter->stack_entries[index].direct == direct)) {
            entry = &emitter->stack_entries[index];
            break;
        }
    }
    if (entry == NULL) {
        char name[64];
        if (emitter->stack_entry_count == emitter->stack_entry_capacity) {
            const size_t capacity =
                emitter->stack_entry_capacity == 0U ? 16U : emitter->stack_entry_capacity * 2U;
            RLlvmStackEntry *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
            if (grown == NULL) {
                return NULL;
            }
            if (emitter->stack_entry_count != 0U) {
                (void)memcpy(
                    grown, emitter->stack_entries, emitter->stack_entry_count * sizeof(*grown));
            }
            r_llvm_free(emitter, emitter->stack_entries);
            emitter->stack_entries = grown;
            emitter->stack_entry_capacity = capacity;
        }
        entry = &emitter->stack_entries[emitter->stack_entry_count];
        entry->symbol = symbol;
        entry->direct = direct;
        (void)snprintf(name,
                       sizeof(name),
                       direct ? "r_stack_entry.%" PRIu32 ".direct" : "r_stack_entry.%" PRIu32,
                       r_llvm_symbol_key(emitter, symbol));
        entry->global = LLVMAddGlobal(emitter->module, r_llvm_int(emitter, 64U), name);
        LLVMSetLinkage(entry->global, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(entry->global, 1);
        LLVMSetExternallyInitialized(entry->global, 1);
        LLVMSetInitializer(entry->global, r_llvm_u64(emitter, 0U));
        emitter->stack_entry_count += 1U;
    }
    return LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), entry->global, "");
}

LLVMValueRef r_llvm_stack_entry(RLlvmEmitter *emitter, RSymbolId symbol) {
    return r_llvm_stack_bound(emitter, symbol, false);
}

LLVMValueRef r_llvm_direct_stack_entry(RLlvmEmitter *emitter, RSymbolId symbol) {
    return r_llvm_stack_bound(emitter, symbol, true);
}

/* The function an entry bounds. */
static LLVMValueRef r_llvm_stack_entry_function(const RLlvmEmitter *emitter,
                                                const RLlvmStackEntry *entry) {
    return entry->direct ? emitter->direct_twins[entry->symbol] : emitter->functions[entry->symbol];
}

typedef struct RLlvmFrame {
    char *name;
    uint64_t size;
} RLlvmFrame;

typedef struct RLlvmFrames {
    RLlvmEmitter *emitter;
    RLlvmFrame *frames;
    size_t count;
    size_t capacity;
} RLlvmFrames;

/* "<location>: N stack bytes in function 'NAME'" from the prologue-epilogue inserter. */
static void r_llvm_stack_remark(LLVMDiagnosticInfoRef info, void *user_data) {
    static const char marker[] = " stack bytes in function '";
    RLlvmFrames *frames = user_data;
    char *text;
    const char *found;
    const char *digits;
    const char *name;
    const char *name_end;

    if (LLVMGetDiagInfoSeverity(info) != LLVMDSRemark) {
        return;
    }
    text = LLVMGetDiagInfoDescription(info);
    found = text == NULL ? NULL : strstr(text, marker);
    if (found != NULL) {
        digits = found;
        while ((digits > text) && (digits[-1] >= '0') && (digits[-1] <= '9')) {
            digits -= 1;
        }
        name = found + sizeof(marker) - 1U;
        name_end = strrchr(name, '\'');
        if ((digits != found) && (name_end != NULL)) {
            if (frames->count == frames->capacity) {
                const size_t capacity = frames->capacity == 0U ? 64U : frames->capacity * 2U;
                RLlvmFrame *grown = r_llvm_allocate(frames->emitter, capacity * sizeof(*grown));
                if (grown != NULL) {
                    if (frames->count != 0U) {
                        (void)memcpy(grown, frames->frames, frames->count * sizeof(*grown));
                    }
                    r_llvm_free(frames->emitter, frames->frames);
                    frames->frames = grown;
                    frames->capacity = capacity;
                }
            }
            if (frames->count < frames->capacity) {
                const size_t length = (size_t)(name_end - name);
                char *copy = r_llvm_allocate(frames->emitter, length + 1U);
                if (copy != NULL) {
                    (void)memcpy(copy, name, length);
                    copy[length] = '\0';
                    frames->frames[frames->count].name = copy;
                    frames->frames[frames->count].size = strtoull(digits, NULL, 10);
                    frames->count += 1U;
                }
            }
        }
    }
    LLVMDisposeMessage(text);
}

static bool r_llvm_frame_size(const RLlvmFrames *frames, const char *name, uint64_t *size) {
    size_t index;

    for (index = 0U; index < frames->count; ++index) {
        if (strcmp(frames->frames[index].name, name) == 0) {
            *size = frames->frames[index].size;
            return true;
        }
    }
    return false;
}

/* The functions of the module with a body, their frames and the defined functions each calls. */
typedef struct RLlvmStackNode {
    LLVMValueRef function;
    uint64_t frame;
    uint32_t depth; /* the @recursion depth of its body, 0 without */
    size_t first_edge;
    size_t edge_count;
    /* Tarjan's components: visit order, lowest reachable order, component, on the stack. */
    size_t order;
    size_t low;
    size_t component;
    bool visited;
    bool on_stack;
} RLlvmStackNode;

typedef struct RLlvmStackComponent {
    uint64_t bound;
} RLlvmStackComponent;

typedef struct RLlvmStackGraph {
    RLlvmEmitter *emitter;
    RLlvmStackNode *nodes;
    size_t count;
    size_t *edges;
    size_t edge_count;
    size_t edge_capacity;
    RLlvmStackComponent *components;
    size_t component_count;
} RLlvmStackGraph;

static RLlvmStackNode *r_llvm_stack_node(RLlvmStackGraph *graph, LLVMValueRef function) {
    size_t index;

    for (index = 0U; index < graph->count; ++index) {
        if (graph->nodes[index].function == function) {
            return &graph->nodes[index];
        }
    }
    return NULL;
}

static bool r_llvm_stack_iterative_drop(LLVMValueRef callee) {
    size_t length = 0U;
    const char *name = LLVMGetValueName2(callee, &length);

    return (length == 24U) && (memcmp(name, "r_runtime_drop_iterative", 24U) == 0);
}

static bool r_llvm_stack_add_edge(RLlvmStackGraph *graph, const RLlvmStackNode *target) {
    if (target == NULL) {
        return true;
    }
    if (graph->edge_count == graph->edge_capacity) {
        const size_t capacity = graph->edge_capacity == 0U ? 256U : graph->edge_capacity * 2U;
        size_t *grown = r_llvm_allocate(graph->emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (graph->edge_count != 0U) {
            (void)memcpy(grown, graph->edges, graph->edge_count * sizeof(*grown));
        }
        r_llvm_free(graph->emitter, graph->edges);
        graph->edges = grown;
        graph->edge_capacity = capacity;
    }
    graph->edges[graph->edge_count++] = (size_t)(target - graph->nodes);
    return true;
}

/* The defined functions a function may have on the stack above its frame: its direct calls, the
   program functions the runtime walks back into during an iterative drop, and the callbacks the
   library makes during its calls (stack edges). */
static bool r_llvm_stack_callees(RLlvmStackGraph *graph, RLlvmStackNode *node) {
    LLVMBasicBlockRef block;
    size_t edge;

    node->first_edge = graph->edge_count;
    for (block = LLVMGetFirstBasicBlock(node->function); block != NULL;
         block = LLVMGetNextBasicBlock(block)) {
        LLVMValueRef instruction;
        for (instruction = LLVMGetFirstInstruction(block); instruction != NULL;
             instruction = LLVMGetNextInstruction(instruction)) {
            LLVMValueRef callee;
            if (LLVMGetInstructionOpcode(instruction) != LLVMCall) {
                continue;
            }
            callee = LLVMGetCalledValue(instruction);
            if (LLVMIsAFunction(callee) == NULL) {
                /* Every indirect call the emitter writes lies in a function that holds it, whose
                   edges name its candidates (r_llvm_check_indirect_calls). Any other is one of
                   imported runtime code, through a pointer the runtime holds to its own function
                   or to type glue, which counts as a call of the runtime (import.c). */
                continue;
            }
            if (r_llvm_guarded_call_marked(graph->emitter, instruction)) {
                /* The callee is an entry with its own bound, required before the call. */
                continue;
            }
            if (r_llvm_stack_iterative_drop(callee)) {
                size_t back;
                for (back = 0U; back < graph->emitter->runtime_callee_count; ++back) {
                    if (!r_llvm_stack_add_edge(
                            graph,
                            r_llvm_stack_node(graph, graph->emitter->runtime_callees[back]))) {
                        return false;
                    }
                }
                continue;
            }
            if (!r_llvm_stack_add_edge(graph, r_llvm_stack_node(graph, callee))) {
                return false;
            }
        }
    }
    for (edge = 0U; edge < graph->emitter->stack_edge_count; ++edge) {
        if ((graph->emitter->stack_edges[edge].caller == node->function) &&
            !r_llvm_stack_add_edge(
                graph, r_llvm_stack_node(graph, graph->emitter->stack_edges[edge].callee))) {
            return false;
        }
    }
    node->edge_count = graph->edge_count - node->first_edge;
    return true;
}

/* The bound below a call into a completed component (tools/compute_stack_entries.py): its frame
   plus the deepest callee for a single function; for a cycle, whose members all carry
   @recursion (R-FUNC-0026), the sum of depth times frame over its members plus the larger of
   the largest frame (the refused activation) and the deepest callee outside it. */
static bool
r_llvm_stack_component(RLlvmStackGraph *graph, const size_t *members, size_t member_count) {
    const size_t component = graph->component_count;
    uint64_t deepest = 0U;
    uint64_t total = 0U;
    uint64_t largest = 0U;
    bool cyclic = member_count > 1U;
    size_t member;

    for (member = 0U; member < member_count; ++member) {
        graph->nodes[members[member]].component = component;
    }
    for (member = 0U; member < member_count; ++member) {
        const RLlvmStackNode *node = &graph->nodes[members[member]];
        size_t edge;
        for (edge = node->first_edge; edge < node->first_edge + node->edge_count; ++edge) {
            const RLlvmStackNode *target = &graph->nodes[graph->edges[edge]];
            if (target->component == component) {
                cyclic = true;
                continue;
            }
            if (graph->components[target->component].bound > deepest) {
                deepest = graph->components[target->component].bound;
            }
        }
        total += (uint64_t)node->depth * node->frame;
        largest = node->frame > largest ? node->frame : largest;
    }
    if (cyclic) {
        for (member = 0U; member < member_count; ++member) {
            if (graph->nodes[members[member]].depth == 0U) {
                return r_llvm_unsupported(graph->emitter, "a recursive call");
            }
        }
        graph->components[component].bound = total + (deepest > largest ? deepest : largest);
    } else {
        graph->components[component].bound = graph->nodes[members[0]].frame + deepest;
    }
    graph->component_count += 1U;
    return true;
}

/* Visits a node: its order, its place on the component stack and its callees. */
static bool r_llvm_stack_visit(
    RLlvmStackGraph *graph, size_t index, size_t *order, size_t *stack, size_t *stack_count) {
    RLlvmStackNode *node = &graph->nodes[index];

    node->visited = true;
    node->order = node->low = (*order)++;
    node->on_stack = true;
    stack[(*stack_count)++] = index;
    return r_llvm_stack_callees(graph, node);
}

/* Tarjan's strongly connected components of what the entries reach, without recursion; a
   component completes after every component it calls into, so its bound is computed when it
   completes. Functions no entry reaches are never on a stack and are not examined. */
static bool r_llvm_stack_components(RLlvmStackGraph *graph) {
    size_t *stack = r_llvm_allocate(graph->emitter, (graph->count + 1U) * sizeof(*stack));
    size_t *work = r_llvm_allocate(graph->emitter, (graph->count + 1U) * sizeof(*work));
    size_t *next = r_llvm_allocate(graph->emitter, (graph->count + 1U) * sizeof(*next));
    size_t stack_count = 0U;
    size_t order = 0U;
    size_t entry;
    bool success = false;

    graph->components =
        r_llvm_allocate(graph->emitter, (graph->count + 1U) * sizeof(*graph->components));
    if ((stack == NULL) || (work == NULL) || (next == NULL) || (graph->components == NULL)) {
        goto cleanup;
    }
    for (entry = 0U; entry < graph->emitter->stack_entry_count; ++entry) {
        const RLlvmStackNode *start = r_llvm_stack_node(
            graph,
            r_llvm_stack_entry_function(graph->emitter, &graph->emitter->stack_entries[entry]));
        size_t work_count = 0U;
        size_t root;
        if (start == NULL) {
            goto cleanup;
        }
        root = (size_t)(start - graph->nodes);
        if (graph->nodes[root].visited) {
            continue;
        }
        work[work_count++] = root;
        next[root] = 0U;
        if (!r_llvm_stack_visit(graph, root, &order, stack, &stack_count)) {
            goto cleanup;
        }
        while (work_count != 0U) {
            const size_t current = work[work_count - 1U];
            RLlvmStackNode *node = &graph->nodes[current];
            if (next[current] < node->edge_count) {
                const size_t target = graph->edges[node->first_edge + next[current]];
                next[current] += 1U;
                if (!graph->nodes[target].visited) {
                    next[target] = 0U;
                    work[work_count++] = target;
                    if (!r_llvm_stack_visit(graph, target, &order, stack, &stack_count)) {
                        goto cleanup;
                    }
                } else if (graph->nodes[target].on_stack &&
                           (graph->nodes[target].order < node->low)) {
                    node->low = graph->nodes[target].order;
                }
                continue;
            }
            work_count -= 1U;
            if (work_count != 0U) {
                RLlvmStackNode *parent = &graph->nodes[work[work_count - 1U]];
                parent->low = node->low < parent->low ? node->low : parent->low;
            }
            if (node->low == node->order) {
                size_t first = stack_count;
                do {
                    first -= 1U;
                    graph->nodes[stack[first]].on_stack = false;
                } while (stack[first] != current);
                if (!r_llvm_stack_component(graph, &stack[first], stack_count - first)) {
                    goto cleanup;
                }
                stack_count = first;
            }
        }
    }
    success = true;

cleanup:
    r_llvm_free(graph->emitter, stack);
    r_llvm_free(graph->emitter, work);
    r_llvm_free(graph->emitter, next);
    return success && (graph->emitter->status == R_FRONTEND_OK);
}

bool r_llvm_stack_recursion(RLlvmEmitter *emitter, LLVMValueRef function, uint32_t depth) {
    size_t index;

    for (index = 0U; index < emitter->recursion_mark_count; ++index) {
        if (emitter->recursion_marks[index].function == function) {
            return (emitter->recursion_marks[index].depth == depth)
                       ? true
                       : r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
    }
    if (emitter->recursion_mark_count == emitter->recursion_mark_capacity) {
        const size_t capacity =
            emitter->recursion_mark_capacity == 0U ? 8U : emitter->recursion_mark_capacity * 2U;
        RLlvmRecursionMark *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (emitter->recursion_mark_count != 0U) {
            (void)memcpy(
                grown, emitter->recursion_marks, emitter->recursion_mark_count * sizeof(*grown));
        }
        r_llvm_free(emitter, emitter->recursion_marks);
        emitter->recursion_marks = grown;
        emitter->recursion_mark_capacity = capacity;
    }
    r_llvm_add_function_attribute(emitter, function, "noinline");
    emitter->recursion_marks[emitter->recursion_mark_count].function = function;
    emitter->recursion_marks[emitter->recursion_mark_count].depth = depth;
    emitter->recursion_mark_count += 1U;
    return true;
}

static const char r_llvm_indirect_attribute[] = "r.stack.indirect";

/* The function of an indirect call holds it for good: a marked function is never inlined, and
   the optimizer turns no direct call into an indirect one, so after it every indirect call is in
   a marked function, whose edges name its candidates. */
void r_llvm_mark_indirect_call(RLlvmEmitter *emitter, LLVMValueRef call) {
    LLVMValueRef function = LLVMGetBasicBlockParent(LLVMGetInstructionParent(call));

    LLVMAddAttributeAtIndex(function,
                            (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                            LLVMCreateStringAttribute(emitter->context,
                                                      r_llvm_indirect_attribute,
                                                      sizeof(r_llvm_indirect_attribute) - 1U,
                                                      "",
                                                      0U));
    r_llvm_add_function_attribute(emitter, function, "noinline");
}

static unsigned r_llvm_guarded_call_kind(RLlvmEmitter *emitter) {
    static const char name[] = "r.stack.guarded";
    return LLVMGetMDKindIDInContext(emitter->context, name, sizeof(name) - 1U);
}

/* A call that r_runtime_task_direct_begin admitted with the bound of its callee (direct.c). Were
   the mark lost, the call would count as an ordinary one, which only raises the bound. */
void r_llvm_mark_guarded_call(RLlvmEmitter *emitter, LLVMValueRef call) {
    LLVMSetMetadata(
        call,
        r_llvm_guarded_call_kind(emitter),
        LLVMMetadataAsValue(emitter->context, LLVMMDNodeInContext2(emitter->context, NULL, 0U)));
}

static bool r_llvm_guarded_call_marked(RLlvmEmitter *emitter, LLVMValueRef call) {
    return LLVMGetMetadata(call, r_llvm_guarded_call_kind(emitter)) != NULL;
}

static bool r_llvm_holds_indirect_calls(LLVMValueRef function) {
    return LLVMGetStringAttributeAtIndex(function,
                                         (LLVMAttributeIndex)LLVMAttributeFunctionIndex,
                                         r_llvm_indirect_attribute,
                                         sizeof(r_llvm_indirect_attribute) - 1U) != NULL;
}

/* Before the optimizer: every indirect call the emitter wrote in a function the bounds count
   (reached from an entry through direct calls, stack edges and the callees of iterative drops)
   lies in a function that holds indirect calls, with the edges of its candidates. The optimizer
   turns no direct call into an indirect one, so after it any other indirect call in a counted
   function is one of imported code (stack.c above). */
static bool r_llvm_check_reach(RLlvmEmitter *emitter,
                               LLVMValueRef function,
                               LLVMValueRef **work,
                               size_t *count,
                               size_t *capacity) {
    size_t index;

    if ((function == NULL) || (LLVMCountBasicBlocks(function) == 0U)) {
        return true;
    }
    for (index = 0U; index < *count; ++index) {
        if ((*work)[index] == function) {
            return true;
        }
    }
    if (*count == *capacity) {
        const size_t grown_capacity = *capacity == 0U ? 64U : *capacity * 2U;
        LLVMValueRef *grown = r_llvm_allocate(emitter, grown_capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (*count != 0U) {
            (void)memcpy(grown, *work, *count * sizeof(*grown));
        }
        r_llvm_free(emitter, *work);
        *work = grown;
        *capacity = grown_capacity;
    }
    (*work)[(*count)++] = function;
    return true;
}

bool r_llvm_check_indirect_calls(RLlvmEmitter *emitter) {
    LLVMValueRef *work = NULL;
    size_t count = 0U;
    size_t capacity = 0U;
    size_t next;
    size_t index;
    bool success = true;

    for (index = 0U; success && (index < emitter->stack_entry_count); ++index) {
        success =
            r_llvm_check_reach(emitter,
                               r_llvm_stack_entry_function(emitter, &emitter->stack_entries[index]),
                               &work,
                               &count,
                               &capacity);
    }
    for (next = 0U; success && (next < count); ++next) {
        LLVMValueRef function = work[next];
        LLVMBasicBlockRef block;
        const bool holder = r_llvm_holds_indirect_calls(function);
        for (index = 0U; success && (index < emitter->stack_edge_count); ++index) {
            if (emitter->stack_edges[index].caller == function) {
                success = r_llvm_check_reach(
                    emitter, emitter->stack_edges[index].callee, &work, &count, &capacity);
            }
        }
        for (block = LLVMGetFirstBasicBlock(function); success && (block != NULL);
             block = LLVMGetNextBasicBlock(block)) {
            LLVMValueRef instruction;
            for (instruction = LLVMGetFirstInstruction(block); success && (instruction != NULL);
                 instruction = LLVMGetNextInstruction(instruction)) {
                LLVMValueRef callee;
                if (LLVMGetInstructionOpcode(instruction) != LLVMCall) {
                    continue;
                }
                callee = LLVMGetCalledValue(instruction);
                if (LLVMIsAFunction(callee) != NULL) {
                    success = r_llvm_check_reach(emitter, callee, &work, &count, &capacity);
                    for (index = 0U; success && r_llvm_stack_iterative_drop(callee) &&
                                     (index < emitter->runtime_callee_count);
                         ++index) {
                        success = r_llvm_check_reach(
                            emitter, emitter->runtime_callees[index], &work, &count, &capacity);
                    }
                } else if (!holder && (LLVMIsAInlineAsm(callee) == NULL)) {
                    size_t length = 0U;
                    const char *name = LLVMGetValueName2(function, &length);
                    success =
                        r_llvm_unsupported_detail(emitter, "an indirect call in", name, length);
                }
            }
        }
    }
    r_llvm_free(emitter, work);
    return success && (emitter->status == R_FRONTEND_OK);
}

bool r_llvm_add_stack_edge(RLlvmEmitter *emitter, LLVMValueRef caller, LLVMValueRef callee) {
    size_t index;

    /* The call that reaches the callee stays in the frame of the caller. */
    r_llvm_add_function_attribute(emitter, caller, "noinline");
    for (index = 0U; index < emitter->stack_edge_count; ++index) {
        if ((emitter->stack_edges[index].caller == caller) &&
            (emitter->stack_edges[index].callee == callee)) {
            return true;
        }
    }
    if (emitter->stack_edge_count == emitter->stack_edge_capacity) {
        const size_t capacity =
            emitter->stack_edge_capacity == 0U ? 16U : emitter->stack_edge_capacity * 2U;
        RLlvmStackEdge *grown = r_llvm_allocate(emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return false;
        }
        if (emitter->stack_edge_count != 0U) {
            (void)memcpy(grown, emitter->stack_edges, emitter->stack_edge_count * sizeof(*grown));
        }
        r_llvm_free(emitter, emitter->stack_edges);
        emitter->stack_edges = grown;
        emitter->stack_edge_capacity = capacity;
    }
    emitter->stack_edges[emitter->stack_edge_count].caller = caller;
    emitter->stack_edges[emitter->stack_edge_count].callee = callee;
    emitter->stack_edge_count += 1U;
    return true;
}

/* One operand of the named metadata `name`: a node of a function name and integers. */
static void r_llvm_stack_report(RLlvmEmitter *emitter,
                                const char *name,
                                LLVMValueRef function,
                                const uint64_t *values,
                                unsigned count) {
    LLVMMetadataRef fields[4];
    size_t length = 0U;
    const char *function_name = LLVMGetValueName2(function, &length);
    unsigned index;

    fields[0] = LLVMMDStringInContext2(emitter->context, function_name, length);
    for (index = 0U; index < count; ++index) {
        fields[index + 1U] = LLVMValueAsMetadata(r_llvm_u64(emitter, values[index]));
    }
    LLVMAddNamedMetadataOperand(
        emitter->module,
        name,
        LLVMMetadataAsValue(emitter->context,
                            LLVMMDNodeInContext2(emitter->context, fields, count + 1U)));
}

/* The measured frames and bounds also stay in the module as named metadata, which tests read
   from --emit=llvm-ir: r.stack.frames holds each defined function with its frame, its
   @recursion depth and the bound below a call into it, r.stack.entries each entry with its
   bound. */
static void r_llvm_keep(LLVMValueRef *kept, size_t *count, LLVMValueRef value) {
    size_t index;

    for (index = 0U; index < *count; ++index) {
        if (kept[index] == value) {
            return;
        }
    }
    kept[(*count)++] = value;
}

/* llvm.compiler.used of every function and constant the measurement refers to after the
   optimizer: entries and their bound constants, both ends of the stack edges, the callees of
   iterative drops and the functions with a @recursion depth. With all_functions every function
   of the program is kept too, as if called from elsewhere: the optimizer neither deletes it nor
   specializes it for the arguments of the calls it sees, so a check proves itself or stays in
   the function's own body. */
bool r_llvm_preserve_stack_functions(RLlvmEmitter *emitter) {
    const RFrontendContext *context = emitter->frontend;
    const bool all_functions =
        (emitter->artifact_options != NULL) && emitter->artifact_options->all_functions;
    const size_t capacity = (emitter->stack_entry_count * 2U) + (emitter->stack_edge_count * 2U) +
                            emitter->runtime_callee_count + emitter->recursion_mark_count +
                            (all_functions ? context->mir_function_count : 0U);
    LLVMValueRef *kept;
    LLVMValueRef used;
    size_t count = 0U;
    size_t index;

    if (capacity == 0U) {
        return true;
    }
    kept = r_llvm_allocate(emitter, capacity * sizeof(*kept));
    if (kept == NULL) {
        return false;
    }
    for (index = 0U; index < emitter->stack_entry_count; ++index) {
        r_llvm_keep(kept, &count, emitter->stack_entries[index].global);
        if (r_llvm_stack_entry_function(emitter, &emitter->stack_entries[index]) != NULL) {
            r_llvm_keep(
                kept, &count, r_llvm_stack_entry_function(emitter, &emitter->stack_entries[index]));
        }
    }
    for (index = 0U; index < emitter->stack_edge_count; ++index) {
        r_llvm_keep(kept, &count, emitter->stack_edges[index].caller);
        r_llvm_keep(kept, &count, emitter->stack_edges[index].callee);
    }
    for (index = 0U; index < emitter->runtime_callee_count; ++index) {
        r_llvm_keep(kept, &count, emitter->runtime_callees[index]);
    }
    for (index = 0U; index < emitter->recursion_mark_count; ++index) {
        r_llvm_keep(kept, &count, emitter->recursion_marks[index].function);
    }
    for (index = 0U; all_functions && (index < context->mir_function_count); ++index) {
        const RSymbolId symbol = context->mir_functions[index].symbol;
        if (!context->semantic_symbols[(size_t)symbol - 1U].is_import &&
            (emitter->functions[symbol] != NULL)) {
            r_llvm_keep(kept, &count, emitter->functions[symbol]);
        }
    }
    used = LLVMAddGlobal(
        emitter->module, LLVMArrayType2(r_llvm_pointer(emitter), count), "llvm.compiler.used");
    LLVMSetLinkage(used, LLVMAppendingLinkage);
    LLVMSetSection(used, "llvm.metadata");
    LLVMSetInitializer(used, LLVMConstArray2(r_llvm_pointer(emitter), kept, count));
    r_llvm_free(emitter, kept);
    return true;
}

bool r_llvm_measure_stack(RLlvmEmitter *emitter) {
    RLlvmFrames frames;
    RLlvmStackGraph graph;
    LLVMModuleRef copy;
    LLVMMemoryBufferRef buffer = NULL;
    LLVMDiagnosticHandler previous_handler;
    void *previous_context;
    LLVMValueRef function;
    char *error = NULL;
    bool success = false;
    size_t count = 0U;
    size_t index;

    if (emitter->stack_entry_count == 0U) {
        return true;
    }
    (void)memset(&frames, 0, sizeof(frames));
    (void)memset(&graph, 0, sizeof(graph));
    frames.emitter = emitter;
    graph.emitter = emitter;
    previous_handler = LLVMContextGetDiagnosticHandler(emitter->context);
    previous_context = LLVMContextGetDiagnosticContext(emitter->context);
    LLVMContextSetDiagnosticHandler(emitter->context, r_llvm_stack_remark, &frames);
    copy = LLVMCloneModule(emitter->module);
    if (LLVMTargetMachineEmitToMemoryBuffer(
            emitter->machine, copy, LLVMObjectFile, &error, &buffer) != 0) {
        (void)fprintf(stderr, "r-front: LLVM code generation failed: %s\n", error);
        LLVMDisposeMessage(error);
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    } else {
        LLVMDisposeMemoryBuffer(buffer);
        success = emitter->status == R_FRONTEND_OK;
    }
    LLVMDisposeModule(copy);
    LLVMContextSetDiagnosticHandler(emitter->context, previous_handler, previous_context);
    if (!success) {
        goto cleanup;
    }
    success = false;
    for (function = LLVMGetFirstFunction(emitter->module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        if (r_llvm_stack_defined(function)) {
            count += 1U;
        }
    }
    graph.nodes = r_llvm_allocate(emitter, count * sizeof(*graph.nodes));
    if (graph.nodes == NULL) {
        goto cleanup;
    }
    for (function = LLVMGetFirstFunction(emitter->module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        size_t length = 0U;
        const char *name;
        if (!r_llvm_stack_defined(function)) {
            continue;
        }
        name = LLVMGetValueName2(function, &length);
        graph.nodes[graph.count].function = function;
        if (!r_llvm_frame_size(&frames, name, &graph.nodes[graph.count].frame)) {
            (void)fprintf(stderr, "r-front: no stack size was measured for %s\n", name);
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            goto cleanup;
        }
        graph.count += 1U;
    }
    for (index = 0U; index < emitter->recursion_mark_count; ++index) {
        RLlvmStackNode *node = r_llvm_stack_node(&graph, emitter->recursion_marks[index].function);
        if (node != NULL) {
            node->depth = emitter->recursion_marks[index].depth;
        }
    }
    if (!r_llvm_stack_components(&graph)) {
        goto cleanup;
    }
    for (index = 0U; index < graph.count; ++index) {
        const RLlvmStackNode *node = &graph.nodes[index];
        const uint64_t values[3] = {
            node->frame, node->depth, graph.components[node->component].bound};
        r_llvm_stack_report(emitter, "r.stack.frames", node->function, values, 3U);
    }
    for (index = 0U; index < emitter->stack_entry_count; ++index) {
        const RLlvmStackEntry *entry = &emitter->stack_entries[index];
        RLlvmStackNode *node =
            r_llvm_stack_node(&graph, r_llvm_stack_entry_function(emitter, entry));
        uint64_t bound;
        if (node == NULL) {
            (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            goto cleanup;
        }
        bound = graph.components[node->component].bound;
        if (bound > r_llvm_target_entry_budget()) {
            size_t length = 0U;
            (void)fprintf(stderr,
                          "r-front: R-DIAG-LIMIT-001 [R-FUNC-0004]: entry %s needs %" PRIu64
                          " bytes of stack, above the target budget of %" PRIu64 "\n",
                          LLVMGetValueName2(node->function, &length),
                          bound,
                          r_llvm_target_entry_budget());
            (void)r_llvm_fail(emitter, R_FRONTEND_LIMIT_EXCEEDED);
            goto cleanup;
        }
        LLVMSetInitializer(entry->global, r_llvm_u64(emitter, bound));
        r_llvm_stack_report(emitter, "r.stack.entries", node->function, &bound, 1U);
    }
    success = true;

cleanup:
    for (index = 0U; index < frames.count; ++index) {
        r_llvm_free(emitter, frames.frames[index].name);
    }
    r_llvm_free(emitter, frames.frames);
    r_llvm_free(emitter, graph.nodes);
    r_llvm_free(emitter, graph.edges);
    r_llvm_free(emitter, graph.components);
    return success;
}
