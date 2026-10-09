# The pinned toolchain of every preset: clang 22.1.8 from the Homebrew llvm@22 keg compiles all C
# of the project and r-front links LLVM of the same release (targets/*.json toolchain section,
# tools/toolchain.lock). tools/check_target_toolchain.py verifies the compiler by version and by
# the digest of its executable.
set(R_LLVM_ROOT "/opt/homebrew/opt/llvm@22" CACHE PATH "Root of the pinned LLVM 22.1.8 installation")

if(NOT EXISTS "${R_LLVM_ROOT}/bin/clang" OR NOT EXISTS "${R_LLVM_ROOT}/bin/llvm-config")
    message(FATAL_ERROR
        "the pinned LLVM 22.1.8 toolchain is not installed at ${R_LLVM_ROOT} "
        "(brew install llvm@22, or configure with -DR_LLVM_ROOT=DIR)")
endif()

set(CMAKE_C_COMPILER "${R_LLVM_ROOT}/bin/clang")
