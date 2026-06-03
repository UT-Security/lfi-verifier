// Large sandbox scheme tests (expected to pass verification).
// See LFI-large for the design.

// Zero-offset memory access uses the [x27, x24] addressing mode.
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFFF
ldr x0, [x27, x24]
---
// Store via the [x27, x24] addressing mode.
// flags: --large --p2size=36
and x24, x1, #0xFFFFFFFFF
str x0, [x27, x24]
---
// Offset memory access builds a full address in x28.
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFFF
add x28, x27, x24
ldr x0, [x28, #8]
---
// Indirect branch through the offset and address registers.
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFFF
add x28, x27, x24
br x28
---
// Stack pointer assignment.
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFFF
add sp, x27, x24
---
// Stack pointer arithmetic rewrite (add/sub sp, sp, ...).
// flags: --large --p2size=36
add x26, sp, #16
and x24, x26, #0xFFFFFFFFF
add sp, x27, x24
---
// Deferred link register guard before ret.
// flags: --large --p2size=36
mov x30, x0
add x30, x27, x24
ret
---
// Guard elimination: a single guard covers multiple accesses to x28.
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFFF
add x28, x27, x24
ldr x1, [x28]
ldr x2, [x28, #8]
ldr x3, [x28, #16]
---
// An x24 of zero (masking xzr) is a valid offset.
// flags: --large --p2size=36
and x24, xzr, #0xFFFFFFFFF
ldr x0, [x27, x24]
---
// The standard 4 GiB uxtw guard remains valid in the large scheme.
// flags: --large --p2size=36
add x28, x27, w0, uxtw
ldr x0, [x28]
---
// The standard 4 GiB uxtw addressing mode remains valid in the large scheme.
// flags: --large --p2size=36
ldr x0, [x27, w5, uxtw]
---
// Authenticated return expansion (retaa -> autiasp; guard; ret).
// flags: --large --p2size=36
.arch_extension pauth
autiasp
and x24, x30, #0xFFFFFFFFF
add x30, x27, x24
ret
---
// The context register option composes with the large scheme.
// flags: --large --p2size=36 --ctxreg
ldr x0, [x25, #16]
---
// p2size=32 (4 GiB): mask is 0xFFFFFFFF.
// flags: --large --p2size=32
and x24, x0, #0xFFFFFFFF
ldr x0, [x27, x24]
---
// p2size=40 (1 TiB): mask is 0xFFFFFFFFFF.
// flags: --large --p2size=40
and x24, x0, #0xFFFFFFFFFF
ldr x0, [x27, x24]
---
// p2size=63: the largest supported mask.
// flags: --large --p2size=63
and x24, x0, #0x7FFFFFFFFFFFFFFF
ldr x0, [x27, x24]
