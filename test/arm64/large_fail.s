// Large sandbox scheme tests (expected to fail verification).
// See LFI-large for the design.

// The and mask must match the configured size exactly (here 0xFFFFFFFFF).
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFF
ldr x0, [x27, x24]
---
// A mask that is too small for the configured size is still rejected: the
// verifier requires exactly 2^p2size - 1.
// flags: --large --p2size=36
and x24, x0, #0xFF
ldr x0, [x27, x24]
---
// A mask for a different (larger) size is rejected.
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFFFF
ldr x0, [x27, x24]
---
// x24 may only be written by `and x24, xN, #mask`, never by mov.
// flags: --large --p2size=36
mov x24, x0
---
// x24 may not be loaded from memory.
// flags: --large --p2size=36
ldr x24, [x28]
---
// x24 may not be written by a pair load.
// flags: --large --p2size=36
ldp x24, x9, [x28]
---
// x24 may not be written by general arithmetic.
// flags: --large --p2size=36
add x24, x0, x1
---
// The offset-setting instruction must be `and`, not the flag-setting `ands`.
// flags: --large --p2size=36
ands x24, x0, #0xFFFFFFFFF
---
// The offset-setting `and` must be 64-bit, not a 32-bit `and w24`.
// flags: --large --p2size=36
and w24, w0, #0xFFFF
---
// The address guard must use the offset register x24, not an arbitrary one.
// flags: --large --p2size=36
add x28, x27, x5
---
// The stack pointer guard must use the offset register x24.
// flags: --large --p2size=36
add sp, x27, x5
---
// The [x27, x24] addressing mode must use the offset register, not any reg.
// flags: --large --p2size=36
ldr x0, [x27, x5]
---
// The [x27, x24] addressing mode must not apply a scale.
// flags: --large --p2size=36
ldr x0, [x27, x24, lsl #3]
---
// The register-offset base must be x27.
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFFF
ldr x0, [x28, x24]
---
// x24 is not a valid indirect branch target.
// flags: --large --p2size=36
and x24, x0, #0xFFFFFFFFF
br x24
---
// An x30 guard that does not use the offset register leaves x30 unguarded.
// flags: --large --p2size=36
mov x30, x0
add x30, x27, x30
ret
