#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

enum LFIBoxType {
    LFI_BOX_FULL,
    LFI_BOX_STORES,
};

struct LFIVOptions {
    // Sandbox type (full, stores-only).
    enum LFIBoxType box;

    // Disable BDD filter (x86-64).
    bool no_bdd;

    // Enable context register (x25 on arm64, r15 on x64).
    // When enabled, the context register is reserved (cannot be modified)
    // and can only be used for 64-bit loads/stores from the address it holds.
    bool ctxreg;

    // Enable the large sandbox scheme (arm64 only). When enabled, x24 is
    // reserved as the offset register and the two-instruction guard sequence
    //     and x24, xN, #((1 << p2size) - 1)
    //     add x28, x27, x24
    // is recognized in addition to the standard 4 GiB guards. See LFI-large.
    bool large;

    // Log2 of the sandbox size for the large sandbox scheme: the sandbox is
    // 2^p2size bytes and the mask (2^p2size - 1) is the logical immediate that
    // every `and x24, xN, #mask` must use. Must be in [32, 63] (i.e. at least
    // 4 GiB). Only consulted when `large` is set.
    unsigned p2size;

    // Callback to print a null-terminated error message if verification fails.
    void (*err)(char *msg, size_t size);
};

struct LFIVerifier {
    // Verifier options.
    struct LFIVOptions opts;

    // Verify the given code buffer, assuming a start address of vaddr.
    bool (*verify)(char *code, size_t size, uintptr_t vaddr, struct LFIVOptions *opts);
};

// Run the arm64 verifier.
bool
lfiv_verify_arm64(char *code, size_t size, uintptr_t addr, struct LFIVOptions *opts);

// Run the x64 verifier.
bool
lfiv_verify_x64(char *code, size_t size, uintptr_t addr, struct LFIVOptions *opts);

// Run the riscv64 verifier.
bool
lfiv_verify_riscv64(char *code, size_t size, uintptr_t addr, struct LFIVOptions *opts);

static inline bool
lfiv_verify(struct LFIVerifier *v, char *code, size_t size, uintptr_t addr)
{
    if (!v->verify)
        return false;
    return v->verify(code, size, addr, &v->opts);
}
