#include "emit_internal.h"
#include "r_frontend.h"

#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "target_llvm.generated.inc"

/* Both targets of the LLVM transition are AArch64 (arm64-apple-darwin and
   aarch64-unknown-linux-gnu); registering the backend is idempotent. */
static void r_llvm_initialize_targets(void) {
    LLVMInitializeAArch64TargetInfo();
    LLVMInitializeAArch64Target();
    LLVMInitializeAArch64TargetMC();
}

/* Writes "reason: first second" into the caller's message buffer. */
static bool r_llvm_message(
    char *message, size_t capacity, const char *reason, const char *first, const char *second) {
    if ((message != NULL) && (capacity != 0U) &&
        (snprintf(message, capacity, "%s: %s %s", reason, first, second) < 0)) {
        message[0] = '\0';
    }
    return false;
}

bool r_frontend_backend_version(char *buffer, size_t capacity) {
    unsigned major = 0U;
    unsigned minor = 0U;
    unsigned patch = 0U;
    int written;

    if ((buffer == NULL) || (capacity == 0U)) {
        return false;
    }
    LLVMGetVersion(&major, &minor, &patch);
    written = snprintf(buffer, capacity, "%u.%u.%u", major, minor, patch);
    return (written > 0) && ((size_t)written < capacity);
}

const char *r_frontend_backend_triple(void) {
    return r_target_llvm_triple;
}

const char *r_frontend_backend_cpu(void) {
    return r_target_llvm_cpu;
}

bool r_frontend_backend_verify(char *message, size_t capacity) {
    char version[32];
    LLVMTargetRef target = NULL;
    LLVMTargetMachineRef machine = NULL;
    LLVMTargetDataRef layout = NULL;
    char *error = NULL;
    char *layout_text = NULL;
    bool success = false;

    if (!r_frontend_backend_version(version, sizeof(version))) {
        return r_llvm_message(message, capacity, "LLVM version is unavailable", "", "");
    }
    if (strcmp(version, r_target_llvm_version) != 0) {
        return r_llvm_message(message,
                              capacity,
                              "linked LLVM differs from toolchain.llvm_version",
                              version,
                              r_target_llvm_version);
    }
    r_llvm_initialize_targets();
    if (LLVMGetTargetFromTriple(r_target_llvm_triple, &target, &error) != 0) {
        (void)r_llvm_message(message,
                             capacity,
                             "LLVM has no target for toolchain.llvm_triple",
                             r_target_llvm_triple,
                             error != NULL ? error : "");
        goto cleanup;
    }
    machine = LLVMCreateTargetMachine(target,
                                      r_target_llvm_triple,
                                      r_target_llvm_cpu,
                                      "",
                                      LLVMCodeGenLevelDefault,
                                      LLVMRelocPIC,
                                      LLVMCodeModelDefault);
    if (machine == NULL) {
        (void)r_llvm_message(message,
                             capacity,
                             "LLVM could not create a target machine",
                             r_target_llvm_triple,
                             r_target_llvm_cpu);
        goto cleanup;
    }
    layout = LLVMCreateTargetDataLayout(machine);
    layout_text = LLVMCopyStringRepOfTargetData(layout);
    if ((layout_text == NULL) || (strcmp(layout_text, r_target_llvm_data_layout) != 0)) {
        (void)r_llvm_message(message,
                             capacity,
                             "LLVM data layout differs from toolchain.llvm_data_layout",
                             layout_text != NULL ? layout_text : "",
                             r_target_llvm_data_layout);
        goto cleanup;
    }
    success = true;

cleanup:
    if (layout_text != NULL) {
        LLVMDisposeMessage(layout_text);
    }
    if (layout != NULL) {
        LLVMDisposeTargetData(layout);
    }
    if (machine != NULL) {
        LLVMDisposeTargetMachine(machine);
    }
    if (error != NULL) {
        LLVMDisposeMessage(error);
    }
    return success;
}

uint64_t r_llvm_target_entry_budget(void) {
    return r_target_llvm_entry_budget_bytes;
}
