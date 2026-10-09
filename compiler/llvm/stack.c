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
   constants the checks read, and the module itself is then compiled for output. */

LLVMValueRef r_llvm_stack_entry(RLlvmEmitter *emitter, RSymbolId symbol) {
    RLlvmStackEntry *entry = NULL;
    size_t index;

    for (index = 0U; index < emitter->stack_entry_count; ++index) {
        if (emitter->stack_entries[index].symbol == symbol) {
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
        (void)snprintf(
            name, sizeof(name), "r_stack_entry.%" PRIu32, r_llvm_symbol_key(emitter, symbol));
        entry->global = LLVMAddGlobal(emitter->module, r_llvm_int(emitter, 64U), name);
        LLVMSetLinkage(entry->global, LLVMPrivateLinkage);
        LLVMSetGlobalConstant(entry->global, 1);
        LLVMSetInitializer(entry->global, r_llvm_u64(emitter, 0U));
        emitter->stack_entry_count += 1U;
    }
    return LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), entry->global, "");
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
                /* An indirect call needs the candidates the emitter marks for it. */
                if (r_llvm_indirect_call_marked(graph->emitter, instruction)) {
                    continue;
                }
                return r_llvm_unsupported(graph->emitter, "an indirect call");
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
            graph, graph->emitter->functions[graph->emitter->stack_entries[entry].symbol]);
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
    emitter->recursion_marks[emitter->recursion_mark_count].function = function;
    emitter->recursion_marks[emitter->recursion_mark_count].depth = depth;
    emitter->recursion_mark_count += 1U;
    return true;
}

static unsigned r_llvm_indirect_kind(RLlvmEmitter *emitter) {
    static const char name[] = "r.stack.indirect";
    return LLVMGetMDKindIDInContext(emitter->context, name, sizeof(name) - 1U);
}

void r_llvm_mark_indirect_call(RLlvmEmitter *emitter, LLVMValueRef call) {
    LLVMSetMetadata(
        call,
        r_llvm_indirect_kind(emitter),
        LLVMMetadataAsValue(emitter->context, LLVMMDNodeInContext2(emitter->context, NULL, 0U)));
}

bool r_llvm_indirect_call_marked(RLlvmEmitter *emitter, LLVMValueRef call) {
    return LLVMGetMetadata(call, r_llvm_indirect_kind(emitter)) != NULL;
}

bool r_llvm_add_stack_edge(RLlvmEmitter *emitter, LLVMValueRef caller, LLVMValueRef callee) {
    size_t index;

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
bool r_llvm_measure_stack(RLlvmEmitter *emitter) {
    static bool remarks_enabled = false;
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
    if (!remarks_enabled) {
        static const char *const arguments[] = {"r-front", "-pass-remarks-analysis=prologepilog"};
        LLVMParseCommandLineOptions(2, arguments, NULL);
        remarks_enabled = true;
    }
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
        if (LLVMCountBasicBlocks(function) != 0U) {
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
        if (LLVMCountBasicBlocks(function) == 0U) {
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
        RLlvmStackNode *node = r_llvm_stack_node(&graph, emitter->functions[entry->symbol]);
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
