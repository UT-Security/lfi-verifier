add $8, %rdi
---
.bundle_align_mode 5
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
add $8, %rdi
jmp foo
nop
add $8, %rdi
.p2align 5
foo:
---
mov %gs:0, %rax
---
leaq 8(%rax), %rax
---
mov 12(%rsp), %rax
---
mov 12(%rip), %rax
---
.bundle_align_mode 5
.bundle_lock
andq %r15, %rdi
andq $0xffffffffffffffe0, %rdi
orq %r14, %rdi
jmpq *%rdi
.bundle_unlock
---
mov %rax, %rsp
andq %r15, %rsp
orq %r14, %rsp
---
.bundle_align_mode 5
.bundle_lock
andq %r15, %rsp
leaq 12(%rsp, %r14), %rsp
.bundle_unlock
---
.bundle_align_mode 5
.bundle_lock
andq $0xfffffffffffffff0, %rsp
andq %r15, %rsp
orq %r14, %rsp
.bundle_unlock
---
cmp %rsp, %rsp
---
.bundle_align_mode 5
movq %rax, %r11
.bundle_lock
andq %r15, %r11
andq $0xffffffffffffffe0, %r11
orq %r14, %r11
callq *%r11
.bundle_unlock
---
leaq 1f(%rip), %r11
jmpq *(%r14)
1:
---
leaq 1f(%rip), %r11
jmpq *8(%r14)
1:
---
leaq 1f(%rip), %r11
jmpq *(%r14)
.p2align 5
1:
---
.bundle_align_mode 5
.bundle_lock
leaq -0x10(%rbp), %rsp
pext %r15, %rsp, %rsp
leaq (%rsp, %r14), %rsp
.bundle_unlock
---
movaps -0x7884(%rip),%xmm4
---
cld
---
std
---
pause
---
.bundle_align_mode 4
.bundle_lock
pext %r15, %rdi, %r11
mov %rax, (%r14, %r11)
.bundle_unlock
---
pext %r15, %rdi, %r11
mov %rax, 32(%r14, %r11)
---
1:
.bundle_align_mode 4
.bundle_lock
pext %r15, %rdi, %rdi
leaq (%r14, %rdi), %rdi
stosq
.bundle_unlock
---
.bundle_align_mode 5
1:
.bundle_lock
pext %r15, %rdi, %rdi
leaq (%r14, %rdi), %rdi
pext %r15, %rsi, %rsi
leaq (%r14, %rsi), %rsi
movsq
.bundle_unlock
---
// flags: --sandbox=stores
mov (%rdi), %rax
---
movq %rdi, %r11
andq %r15, %r11
movq $0x0, (%r14, %r11)
---
// flags: --sandbox=stores
leaq (%r9, %rdi), %r11
andq %r15, %r11
add %rdi, (%r14, %r11)
---
nopq (%rax, %rax)
andq %r15, %r11
movq %rax, (%r14, %r11)
---
pext %r15, %rbp, %rdi
lea (%r14, %rdi), %rdi
mov %bh, -0x4e(%rdi)
---
mov %rax, %rdi
jmp unaligned
unaligned:
mov %rsi, %rdi
---
mov %rax, %rdi
unaligned:
mov %rax, %rsi
jmp unaligned
