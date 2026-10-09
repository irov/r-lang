#include "emit_internal.h"

#include <llvm-c/BitReader.h>
#include <llvm-c/Comdat.h>
#include <llvm-c/Core.h>
#include <llvm-c/Linker.h>

#include <string.h>

/* The runtime and library C a program calls, optimized with it (B7.2). Before the optimizer runs,
   the bitcode module that defines each C function the program declares is linked into the
   program module, with its external definitions available_externally: the optimizer may inline
   or fold them, never emits them, and the program still links the archive that defines them, so
   no state is duplicated. Callees of imported functions are imported in a second and a third
   round. The stack bound of every entry stays computed on the final code (stack.c): an imported
   body inlined into a program function becomes part of its measured frame, its indirect calls
   (type glue and runtime hooks the runtime holds pointers to) count as calls of the runtime, as
   they did inside the archive's function, and code the measurement could not follow is not
   imported. A function too large to inline, on a cycle of calls, which carries no @recursion
   depth, with an invoke, callbr or inline assembly, or calling such a function keeps only its
   declaration; a module
   with mutable or thread-local state of its own, which an internal copy would duplicate, or with
   module-level assembly is not imported. */

enum {
    R_LLVM_IMPORT_ROUNDS = 2,
    /* Instructions of the largest function imported: a larger body is not inlined, and only
       costs the optimizer its time. */
    R_LLVM_IMPORT_FUNCTION_SIZE = 300U,
    /* Bitcode bytes one program may import, to bound the time the optimizer spends on them. */
    R_LLVM_IMPORT_BUDGET = 8U * 1024U * 1024U
};

typedef struct RLlvmImport {
    RLlvmEmitter *emitter;
    const RFrontendArtifactOptions *options;
    /* The data of each module imported or refused, so another of its symbols does not load it
       again. */
    const uint8_t **seen;
    size_t seen_count;
    size_t seen_capacity;
    size_t imported_bytes;
} RLlvmImport;

static bool r_llvm_import_seen(RLlvmImport *import, const uint8_t *data) {
    size_t index;

    for (index = 0U; index < import->seen_count; ++index) {
        if (import->seen[index] == data) {
            return true;
        }
    }
    if (import->seen_count == import->seen_capacity) {
        const size_t capacity = import->seen_capacity == 0U ? 32U : import->seen_capacity * 2U;
        const uint8_t **grown = r_llvm_allocate(import->emitter, capacity * sizeof(*grown));
        if (grown == NULL) {
            return true;
        }
        if (import->seen_count != 0U) {
            (void)memcpy(grown, import->seen, import->seen_count * sizeof(*grown));
        }
        r_llvm_free(import->emitter, import->seen);
        import->seen = grown;
        import->seen_capacity = capacity;
    }
    import->seen[import->seen_count++] = data;
    return false;
}

static bool r_llvm_is_local(LLVMLinkage linkage) {
    return (linkage == LLVMInternalLinkage) || (linkage == LLVMPrivateLinkage);
}

/* The defined functions of an imported module and what the import may keep of each. */
typedef struct RLlvmImportGraph {
    LLVMValueRef *functions;
    /* 1 for a function the program may not inline: it makes an indirect call, lies on a cycle of
       calls, or calls such a function. */
    uint8_t *refused;
    uint8_t *visited;
    size_t *stack;
    size_t count;
} RLlvmImportGraph;

static size_t r_llvm_import_index(const RLlvmImportGraph *graph, LLVMValueRef function) {
    size_t index;

    for (index = 0U; index < graph->count; ++index) {
        if (graph->functions[index] == function) {
            return index;
        }
    }
    return graph->count;
}

/* The defined functions `caller` calls directly, each once; there are at most graph->count. */
static size_t r_llvm_import_callees(const RLlvmImportGraph *graph,
                                    size_t caller,
                                    size_t *callees,
                                    size_t capacity) {
    LLVMBasicBlockRef block;
    size_t count = 0U;

    for (block = LLVMGetFirstBasicBlock(graph->functions[caller]); block != NULL;
         block = LLVMGetNextBasicBlock(block)) {
        LLVMValueRef instruction;
        for (instruction = LLVMGetFirstInstruction(block); instruction != NULL;
             instruction = LLVMGetNextInstruction(instruction)) {
            size_t callee;
            size_t known = 0U;
            if (LLVMGetInstructionOpcode(instruction) != LLVMCall) {
                continue;
            }
            callee = r_llvm_import_index(graph, LLVMGetCalledValue(instruction));
            while ((known < count) && (callees[known] != callee)) {
                known += 1U;
            }
            if ((callee != graph->count) && (known == count) && (count < capacity)) {
                callees[count++] = callee;
            }
        }
    }
    return count;
}

/* Whether a walk of direct calls from the callees of `start` comes back to it. */
static bool r_llvm_import_on_cycle(RLlvmImportGraph *graph, size_t start, size_t *callees) {
    size_t depth = 0U;

    (void)memset(graph->visited, 0, graph->count);
    graph->stack[depth++] = start;
    while (depth != 0U) {
        const size_t current = graph->stack[--depth];
        const size_t count = r_llvm_import_callees(graph, current, callees, graph->count);
        size_t index;
        for (index = 0U; index < count; ++index) {
            if (callees[index] == start) {
                return true;
            }
            if (!graph->visited[callees[index]]) {
                graph->visited[callees[index]] = 1U;
                graph->stack[depth++] = callees[index];
            }
        }
    }
    return false;
}

/* Whether the function is too large to inline or has a control transfer the import does not
   take (invoke, callbr, a call of inline assembly). An indirect call is taken: it calls a runtime
   function or type glue through a pointer of the runtime, which the stack bound treats as the call
   of a runtime function wherever it is inlined (stack.c). */
static bool r_llvm_import_untaken(LLVMValueRef function) {
    LLVMBasicBlockRef block;
    uint32_t size = 0U;

    for (block = LLVMGetFirstBasicBlock(function); block != NULL;
         block = LLVMGetNextBasicBlock(block)) {
        LLVMValueRef instruction;
        for (instruction = LLVMGetFirstInstruction(block); instruction != NULL;
             instruction = LLVMGetNextInstruction(instruction)) {
            const LLVMOpcode opcode = LLVMGetInstructionOpcode(instruction);
            LLVMValueRef callee;
            if ((opcode == LLVMInvoke) || (opcode == LLVMCallBr) ||
                (++size > R_LLVM_IMPORT_FUNCTION_SIZE)) {
                return true;
            }
            if (opcode != LLVMCall) {
                continue;
            }
            callee = LLVMGetCalledValue(instruction);
            if (LLVMIsAInlineAsm(callee) != NULL) {
                return true;
            }
        }
    }
    return false;
}

/* Turns a definition into a declaration: every instruction loses its uses, then the body goes. */
static void r_llvm_import_strip(LLVMValueRef function) {
    LLVMBasicBlockRef block;

    for (block = LLVMGetFirstBasicBlock(function); block != NULL;
         block = LLVMGetNextBasicBlock(block)) {
        LLVMValueRef instruction;
        for (instruction = LLVMGetFirstInstruction(block); instruction != NULL;
             instruction = LLVMGetNextInstruction(instruction)) {
            if (LLVMGetTypeKind(LLVMTypeOf(instruction)) != LLVMVoidTypeKind) {
                LLVMReplaceAllUsesWith(instruction, LLVMGetPoison(LLVMTypeOf(instruction)));
            }
        }
    }
    for (block = LLVMGetFirstBasicBlock(function); block != NULL;
         block = LLVMGetNextBasicBlock(block)) {
        LLVMValueRef instruction;
        while ((instruction = LLVMGetFirstInstruction(block)) != NULL) {
            LLVMInstructionEraseFromParent(instruction);
        }
    }
    while ((block = LLVMGetFirstBasicBlock(function)) != NULL) {
        LLVMDeleteBasicBlock(block);
    }
}

/* Whether anything of the module may be imported, after the functions the program may not inline
   lost their bodies (see the comment at the top). */
static bool r_llvm_import_admissible(RLlvmEmitter *emitter, LLVMModuleRef module) {
    RLlvmImportGraph graph;
    LLVMValueRef global;
    LLVMValueRef function;
    size_t *callees;
    size_t length = 0U;
    size_t index;
    size_t kept = 0U;
    bool changed = true;

    (void)LLVMGetModuleInlineAsm(module, &length);
    if (length != 0U) {
        return false;
    }
    for (global = LLVMGetFirstGlobal(module); global != NULL; global = LLVMGetNextGlobal(global)) {
        if (!LLVMIsDeclaration(global) && r_llvm_is_local(LLVMGetLinkage(global)) &&
            (!LLVMIsGlobalConstant(global) || LLVMIsThreadLocal(global))) {
            return false;
        }
    }
    (void)memset(&graph, 0, sizeof(graph));
    for (function = LLVMGetFirstFunction(module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        graph.count += LLVMCountBasicBlocks(function) != 0U ? 1U : 0U;
    }
    if (graph.count == 0U) {
        return false;
    }
    graph.functions = r_llvm_allocate(emitter, graph.count * sizeof(*graph.functions));
    graph.refused = r_llvm_allocate(emitter, graph.count);
    graph.visited = r_llvm_allocate(emitter, graph.count);
    graph.stack = r_llvm_allocate(emitter, (graph.count + 1U) * sizeof(*graph.stack));
    callees = r_llvm_allocate(emitter, graph.count * sizeof(*callees));
    if ((graph.functions != NULL) && (graph.refused != NULL) && (graph.visited != NULL) &&
        (graph.stack != NULL) && (callees != NULL)) {
        size_t at = 0U;
        for (function = LLVMGetFirstFunction(module); function != NULL;
             function = LLVMGetNextFunction(function)) {
            if (LLVMCountBasicBlocks(function) != 0U) {
                graph.functions[at++] = function;
            }
        }
        for (index = 0U; index < graph.count; ++index) {
            graph.refused[index] = r_llvm_import_untaken(graph.functions[index]) ||
                                           r_llvm_import_on_cycle(&graph, index, callees)
                                       ? 1U
                                       : 0U;
        }
        while (changed) {
            changed = false;
            for (index = 0U; index < graph.count; ++index) {
                const size_t count = r_llvm_import_callees(&graph, index, callees, graph.count);
                size_t callee;
                for (callee = 0U; !graph.refused[index] && (callee < count); ++callee) {
                    if (graph.refused[callees[callee]]) {
                        graph.refused[index] = 1U;
                        changed = true;
                    }
                }
            }
        }
        for (index = 0U; index < graph.count; ++index) {
            if (graph.refused[index] && !r_llvm_is_local(LLVMGetLinkage(graph.functions[index]))) {
                r_llvm_import_strip(graph.functions[index]);
            } else if (!graph.refused[index]) {
                kept += 1U;
            }
        }
        /* A refused local function is deleted once nothing calls it. */
        changed = true;
        while (changed) {
            changed = false;
            for (index = 0U; index < graph.count; ++index) {
                if ((graph.functions[index] != NULL) && graph.refused[index] &&
                    r_llvm_is_local(LLVMGetLinkage(graph.functions[index])) &&
                    (LLVMGetFirstUse(graph.functions[index]) == NULL)) {
                    r_llvm_import_strip(graph.functions[index]);
                    LLVMDeleteFunction(graph.functions[index]);
                    graph.functions[index] = NULL;
                    changed = true;
                }
            }
        }
        for (index = 0U; index < graph.count; ++index) {
            /* A refused local function still called by another refused one remains defined. */
            if ((graph.functions[index] != NULL) && graph.refused[index]) {
                kept = 0U;
            }
        }
    }
    r_llvm_free(emitter, graph.functions);
    r_llvm_free(emitter, graph.refused);
    r_llvm_free(emitter, graph.visited);
    r_llvm_free(emitter, graph.stack);
    r_llvm_free(emitter, callees);
    return kept != 0U;
}

static void r_llvm_import_drop_global(LLVMModuleRef module, const char *name) {
    LLVMValueRef global = LLVMGetNamedGlobal(module, name);

    if (global != NULL) {
        LLVMDeleteGlobal(global);
    }
}

/* The definitions the program may use but never emits. */
static void r_llvm_import_externalize(LLVMModuleRef module) {
    LLVMValueRef global;
    LLVMValueRef function;

    r_llvm_import_drop_global(module, "llvm.global_ctors");
    r_llvm_import_drop_global(module, "llvm.global_dtors");
    r_llvm_import_drop_global(module, "llvm.used");
    r_llvm_import_drop_global(module, "llvm.compiler.used");
    for (global = LLVMGetFirstGlobal(module); global != NULL; global = LLVMGetNextGlobal(global)) {
        if (LLVMIsDeclaration(global) || r_llvm_is_local(LLVMGetLinkage(global))) {
            continue;
        }
        LLVMSetComdat(global, NULL);
        if (LLVMIsGlobalConstant(global) && !LLVMIsThreadLocal(global)) {
            LLVMSetLinkage(global, LLVMAvailableExternallyLinkage);
        } else {
            /* Mutable state stays the archive's: the program refers to its one definition. */
            LLVMSetInitializer(global, NULL);
            LLVMSetLinkage(global, LLVMExternalLinkage);
        }
    }
    for (function = LLVMGetFirstFunction(module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        if (LLVMCountBasicBlocks(function) == 0U) {
            continue;
        }
        /* The program's target machine decides the subtarget of every function it inlines. */
        LLVMRemoveStringAttributeAtIndex(
            function, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, "target-cpu", 10U);
        LLVMRemoveStringAttributeAtIndex(
            function, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, "target-features", 15U);
        LLVMRemoveStringAttributeAtIndex(
            function, (LLVMAttributeIndex)LLVMAttributeFunctionIndex, "tune-cpu", 8U);
        if (!r_llvm_is_local(LLVMGetLinkage(function))) {
            LLVMSetComdat(function, NULL);
            LLVMSetLinkage(function, LLVMAvailableExternallyLinkage);
        }
    }
}

/* Imported code behaves as the archive code it stands for. A harness (--rename-symbol) hooks
   the program's own calls and the shims the driver compiles with its macros, never the calls
   inside the archives, so a module that defines or calls a renamed function is not imported:
   the program then calls the archive or the driver's shim as before. */
static bool r_llvm_import_untouched(const RLlvmEmitter *emitter, LLVMModuleRef module) {
    LLVMValueRef function;

    for (function = LLVMGetFirstFunction(module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        size_t length = 0U;
        const char *name = LLVMGetValueName2(function, &length);
        if (r_llvm_renamed(emitter, name) != name) {
            return false;
        }
    }
    return true;
}

/* Imports the module that defines `name`, if the loader has one and it is admissible. */
static bool r_llvm_import_symbol(RLlvmImport *import, const char *name) {
    RLlvmEmitter *emitter = import->emitter;
    const uint8_t *data = NULL;
    size_t length = 0U;
    LLVMMemoryBufferRef buffer;
    LLVMModuleRef module = NULL;
    size_t layout_length = 0U;

    if (!import->options->load_bitcode(import->options->bitcode_user_data, name, &data, &length) ||
        (data == NULL) || (length == 0U) || r_llvm_import_seen(import, data) ||
        (length > R_LLVM_IMPORT_BUDGET - import->imported_bytes)) {
        return true;
    }
    buffer = LLVMCreateMemoryBufferWithMemoryRange((const char *)data, length, "r-bitcode", 0);
    if (LLVMParseBitcodeInContext2(emitter->context, buffer, &module) != 0) {
        LLVMDisposeMemoryBuffer(buffer);
        return true;
    }
    LLVMDisposeMemoryBuffer(buffer);
    {
        const char *layout = LLVMGetDataLayoutStr(module);
        char *expected = LLVMCopyStringRepOfTargetData(emitter->data);
        const bool same_layout = strcmp(layout, expected) == 0;
        layout_length = strlen(layout);
        LLVMDisposeMessage(expected);
        if (!same_layout || (layout_length == 0U) || !r_llvm_import_untouched(emitter, module) ||
            !r_llvm_import_admissible(emitter, module)) {
            LLVMDisposeModule(module);
            return emitter->status == R_FRONTEND_OK;
        }
    }
    /* The program's triple names the deployment target of the manifest. */
    LLVMSetTarget(module, LLVMGetTarget(emitter->module));
    r_llvm_import_externalize(module);
    if (LLVMLinkModules2(emitter->module, module) != 0) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    import->imported_bytes += length;
    return true;
}

/* The names of the functions the module declares and does not define, copied: linking a
   module replaces a declaration it defines. */
static char **r_llvm_import_wanted(RLlvmEmitter *emitter, size_t *count) {
    LLVMValueRef function;
    char **names;
    size_t total = 0U;

    *count = 0U;
    for (function = LLVMGetFirstFunction(emitter->module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        total += 1U;
    }
    names = r_llvm_allocate(emitter, (total + 1U) * sizeof(*names));
    if (names == NULL) {
        return NULL;
    }
    for (function = LLVMGetFirstFunction(emitter->module); function != NULL;
         function = LLVMGetNextFunction(function)) {
        size_t length = 0U;
        const char *name = LLVMGetValueName2(function, &length);
        char *copy;
        if ((LLVMCountBasicBlocks(function) != 0U) || (LLVMGetIntrinsicID(function) != 0U) ||
            (length == 0U) || (strncmp(name, "llvm.", 5U) == 0)) {
            continue;
        }
        copy = r_llvm_allocate(emitter, length + 1U);
        if (copy == NULL) {
            break;
        }
        (void)memcpy(copy, name, length);
        copy[length] = '\0';
        names[(*count)++] = copy;
    }
    return names;
}

bool r_llvm_import_bitcode(RLlvmEmitter *emitter, uint32_t level) {
    const RFrontendArtifactOptions *options = emitter->artifact_options;
    RLlvmImport import;
    uint32_t round;
    bool success = true;

    if ((level == 0U) || (options == NULL) || (options->load_bitcode == NULL)) {
        return true;
    }
    (void)memset(&import, 0, sizeof(import));
    import.emitter = emitter;
    import.options = options;
    for (round = 0U; success && (round < R_LLVM_IMPORT_ROUNDS); ++round) {
        const size_t before = import.seen_count;
        size_t count = 0U;
        size_t index;
        char **names = r_llvm_import_wanted(emitter, &count);
        if (names == NULL) {
            success = false;
            break;
        }
        for (index = 0U; index < count; ++index) {
            LLVMValueRef function = LLVMGetNamedFunction(emitter->module, names[index]);
            if (success && (function != NULL) && (LLVMCountBasicBlocks(function) == 0U)) {
                success = r_llvm_import_symbol(&import, names[index]);
            }
            r_llvm_free(emitter, names[index]);
        }
        r_llvm_free(emitter, names);
        if (import.seen_count == before) {
            break;
        }
    }
    r_llvm_free(emitter, import.seen);
    return success && (emitter->status == R_FRONTEND_OK);
}
