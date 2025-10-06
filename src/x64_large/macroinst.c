struct MacroInst {
    int size;
    int ninstr;
};

static bool assert_reg(FdInstr *instr, uint8_t op_index, FdReg reg, uint8_t op_size) {
    return (FD_OP_TYPE(instr, op_index) == FD_OT_REG &&
        FD_OP_REG(instr, op_index) == reg &&
        FD_OP_SIZE(instr, op_index) == op_size);
}

static struct MacroInst macroinst_stos(struct Verifier *v, uint8_t *buf, size_t size) {
    // stos becomes:
    //
    // pext %r15 %rdi, %rdi
    // leaq (%r14, %rdi), %rdi
    // rep stosq

    FdInstr i_pext, i_lea, i_stos;
    size_t offset = 0;

    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_pext) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_pext.size;
    if (FD_TYPE(&i_pext) != FDI_PEXT ||
        !assert_reg(&i_pext, 0, FD_REG_DI, 8) ||
        !assert_reg(&i_pext, 1, FD_REG_DI, 8) ||
        !assert_reg(&i_pext, 2, FD_REG_R15, 8)) {
        return (struct MacroInst){-1, 0};
    }

    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_lea) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_lea.size;
    if (FD_TYPE(&i_lea) != FDI_LEA ||
        !assert_reg(&i_lea, 0, FD_REG_DI, 8) ||
        FD_OP_TYPE(&i_lea, 1) != FD_OT_MEM ||
        FD_OP_BASE(&i_lea, 1) != FD_REG_R14 ||
        FD_OP_INDEX(&i_lea, 1) != FD_REG_DI ||
        FD_OP_DISP(&i_lea, 1) != 0 ||
        FD_OP_SCALE(&i_lea, 1) != 0){
        return (struct MacroInst){-1, 0};
    }

    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_stos) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_stos.size;
    if (FD_TYPE(&i_stos) != FDI_STOS) {
        return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, 3};
}

static struct MacroInst macroinst_movs(struct Verifier *v, uint8_t *buf, size_t size) {
    // movs becomes:
    // pext %r15, %rdi, %rdi
    // leaq (%r14, %rdi), %rdi
    // pext %r15, %rsi, %rsi
    // leaq (%r14, %rsi), %rsi
    // movsq

    FdInstr i_pext, i_lea, i_pext2, i_lea2, i_movs;
    size_t offset = 0;

    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_pext) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_pext.size;
    if (FD_TYPE(&i_pext) != FDI_PEXT ||
        !assert_reg(&i_pext, 0, FD_REG_DI, 8) ||
        !assert_reg(&i_pext, 1, FD_REG_DI, 8) ||
        !assert_reg(&i_pext, 1, FD_REG_R15, 8)) {
        return (struct MacroInst){-1, 0};
    }

    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_lea) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_lea.size;
    if (FD_TYPE(&i_lea) != FDI_LEA ||
        !assert_reg(&i_lea, 0, FD_REG_DI, 8) ||
        FD_OP_TYPE(&i_lea, 1) != FD_OT_MEM ||
        FD_OP_BASE(&i_lea, 1) != FD_REG_R14 ||
        FD_OP_INDEX(&i_lea, 1) != FD_REG_DI ||
        FD_OP_DISP(&i_lea, 1) != 0 ||
        FD_OP_SCALE(&i_lea, 1) != 0){
        return (struct MacroInst){-1, 0};
    }

    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_pext2) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_pext2.size;
    if (FD_TYPE(&i_pext2) != FDI_PEXT ||
        !assert_reg(&i_pext2, 0, FD_REG_SI, 8) ||
        !assert_reg(&i_pext2, 1, FD_REG_SI, 8) ||
        !assert_reg(&i_pext2, 2, FD_REG_R15, 8)) {
        return (struct MacroInst){-1, 0};
    }

    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_lea2) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_lea2.size;
    if (FD_TYPE(&i_lea2) != FDI_LEA ||
        !assert_reg(&i_lea2, 0, FD_REG_SI, 8) ||
        FD_OP_TYPE(&i_lea2, 1) != FD_OT_MEM ||
        FD_OP_BASE(&i_lea2, 1) != FD_REG_R14 ||
        FD_OP_INDEX(&i_lea2, 1) != FD_REG_SI ||
        FD_OP_DISP(&i_lea2, 1) != 0 ||
        FD_OP_SCALE(&i_lea2, 1) != 0){
        return (struct MacroInst){-1, 0};
    }

    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_movs) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_movs.size;
    if (FD_TYPE(&i_movs) != FDI_MOVS) {
        return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, 5};
}

static struct MacroInst macroinst_jmp(struct Verifier *v, uint8_t *buf, size_t size) {
    // andq %r15, %rX
    // andq $0xffffffffffffffe0, %rX
    // orq %r14, %rX
    // jmp *(%rX)

    FdInstr i_and, i_and2, i_or, i_jmp;
    size_t offset = 0;
    if (fd_decode(&buf[0], size, 64, 0, &i_and) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_and.size;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_and2) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_and2.size;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_or) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_or.size;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_jmp) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_jmp.size;

    if (FD_TYPE(&i_and) != FDI_AND ||
            !assert_reg(&i_and, 1, FD_REG_R15, 8) ||
            FD_OP_TYPE(&i_and, 0) != FD_OT_REG ||
            reserved(&i_and, 0) ||
            FD_OP_SIZE(&i_and, 0) != 8)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_and2) != FDI_AND ||
            FD_OP_TYPE(&i_and2, 0) != FD_OT_REG ||
            FD_OP_REG(&i_and2, 0) != FD_OP_REG(&i_and, 0) ||
            FD_OP_SIZE(&i_and2, 0) != 8 ||
            FD_OP_TYPE(&i_and2, 1) != FD_OT_IMM ||
            FD_OP_IMM(&i_and2, 1) != 0xffffffffffffffe0)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_or) != FDI_OR ||
            FD_OP_TYPE(&i_or, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_or, 1) != FD_OT_REG ||
            !assert_reg(&i_or, 0, FD_OP_REG(&i_and2, 0), 8) ||
            !assert_reg(&i_or, 1, FD_REG_R14, 8))
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_jmp) != FDI_JMP ||
            FD_OP_TYPE(&i_jmp, 0) != FD_OT_REG ||
            FD_OP_REG(&i_jmp, 0) != FD_OP_REG(&i_and2, 0)
        )
        return (struct MacroInst){-1, 0};

    return (struct MacroInst){offset, 4};
}

static bool okdisp(int64_t disp) {
    return (disp % 8 == 0) && (disp < 256);
}

static struct MacroInst macroinst_rtcall(struct Verifier *v, uint8_t *buf, size_t size) {
    // leaq 1f(%rip), %r11
    // jmpq *N(%r14)
    // 1:
    FdInstr i_lea, i_jmp;
    if (fd_decode(&buf[0], size, 64, 0, &i_lea) < 0)
        return (struct MacroInst){-1, 0};
    if (fd_decode(&buf[i_lea.size], size - i_lea.size, 64, 0, &i_jmp) < 0)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_lea) != FDI_LEA ||
            FD_OP_TYPE(&i_lea, 0) != FD_OT_REG ||
            FD_OP_REG(&i_lea, 0) != FD_REG_R11 ||
            FD_OP_TYPE(&i_lea, 1) != FD_OT_MEM ||
            FD_OP_BASE(&i_lea, 1) != FD_REG_IP)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_jmp) != FDI_JMP ||
            FD_OP_TYPE(&i_jmp, 0) != FD_OT_MEM ||
            FD_OP_BASE(&i_jmp, 0) != FD_REG_R14 ||
            FD_OP_INDEX(&i_jmp, 0) != FD_REG_NONE ||
            FD_OP_SCALE(&i_jmp, 0) != 0 ||
            !okdisp(FD_OP_DISP(&i_jmp, 0)))
        return (struct MacroInst){-1, 0};

    // Return target can either be the next instruction or can be some
    // bundle-aligned location.
    uintptr_t ret = v->addr + i_lea.size + FD_OP_DISP(&i_lea, 1);
    bool ok = FD_OP_DISP(&i_lea, 1) == i_jmp.size || ret % v->bundlesize == 0;
    if (!ok)
        return (struct MacroInst){-1, 0};

    return (struct MacroInst){i_lea.size + i_jmp.size, 2};
}

static struct MacroInst macroinst_call(struct Verifier *v, uint8_t *buf, size_t size) {
    size_t bundlesize = 32;

    // TODO: this relies on a movsq instruction outside of the bundle
    // andq %r15, %r11
    // andq $0xffffffffffffffe0, %r11
    // orq %r14, %r11
    // callq *%r11

    FdInstr i_and, i_and2, i_or, i_jmp;
    size_t offset = 0;
    if (fd_decode(&buf[0], size, 64, 0, &i_and) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_and.size;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_and2) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_and2.size;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_or) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_or.size;
    //TODO: why did this eat nops continuously in the previous code?
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_jmp) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_jmp.size;

    if (FD_TYPE(&i_and) != FDI_AND ||
            !assert_reg(&i_and, 1, FD_REG_R15, 8) ||
            FD_OP_TYPE(&i_and, 0) != FD_OT_REG ||
            // reserved(&i_and, 0) ||
            !assert_reg(&i_and, 0, FD_REG_R11, 8) ||
            FD_OP_SIZE(&i_and, 0) != 8)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_and2) != FDI_AND ||
            FD_OP_TYPE(&i_and2, 0) != FD_OT_REG ||
            FD_OP_REG(&i_and2, 0) != FD_OP_REG(&i_and, 0) ||
            FD_OP_SIZE(&i_and2, 0) != 8 ||
            FD_OP_TYPE(&i_and2, 1) != FD_OT_IMM ||
            FD_OP_IMM(&i_and2, 1) != 0xffffffffffffffe0)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_or) != FDI_OR ||
            FD_OP_TYPE(&i_or, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_or, 1) != FD_OT_REG ||
            !assert_reg(&i_or, 0, FD_OP_REG(&i_and2, 0), 8) ||
            !assert_reg(&i_or, 1, FD_REG_R14, 8))
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_jmp) != FDI_CALL ||
            FD_OP_TYPE(&i_jmp, 0) != FD_OT_REG ||
            FD_OP_REG(&i_jmp, 0) != FD_OP_REG(&i_and2, 0)
        )
        return (struct MacroInst){-1, 0};

    return (struct MacroInst){offset, 4};
}
#define GUARD_SZ 81920
bool check_unsafe_store(FdInstr* inst, uint32_t op) {
    return
        FD_OP_BASE(inst, op) != FD_REG_R14 ||
        FD_OP_INDEX(inst, op) != FD_REG_R11 ||
        FD_OP_SCALE(inst, op) != 0 ||
        FD_OP_DISP(inst, op) > GUARD_SZ ||
        FD_OP_DISP(inst, op) < -GUARD_SZ;
}

static struct MacroInst macroinst_store_pext(struct Verifier *v, uint8_t *buf, size_t size) {
    // note: for  SIB, the actual address will be moved
    // into r11 beforehand
    // this will not work if we cannot reserve r11
    // pext %r15, %rX, %r11
    // mov %rX, (%r14, %r11)

    FdInstr i_store, i_pext;
    if (fd_decode(&buf[0], size, 64, 0, &i_pext) < 0)
        return (struct MacroInst){-1, 0};
    if (fd_decode(&buf[i_pext.size], size - i_pext.size, 64, 0, &i_store) < 0)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_pext) != FDI_PEXT ||
            FD_OP_TYPE(&i_pext, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_pext, 1) != FD_OT_REG ||
            FD_OP_TYPE(&i_pext, 2) != FD_OT_REG ||
            FD_OP_REG(&i_pext, 2) != FD_REG_R15 ||
            FD_OP_REG(&i_pext, 0) != FD_REG_R11
        )
        return (struct MacroInst){-1, 0};

    if(FD_TYPE(&i_store) == FDI_XCHG ||
        FD_TYPE(&i_store) == FDI_CMPXCHG) {
        if((FD_OP_TYPE(&i_store, 0) == FD_OT_MEM &&
            check_unsafe_store(&i_store, 0)) ||
            (FD_OP_TYPE(&i_store, 1) == FD_OT_MEM &&
                check_unsafe_store(&i_store, 1))
           )
            return (struct MacroInst){-1, 0};
    } else {
        if (FD_OP_TYPE(&i_store, 0) != FD_OT_MEM ||
            check_unsafe_store(&i_store, 0))
            return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){i_pext.size + i_store.size, 2};
}

static struct MacroInst macroinst_store_and(struct Verifier *v, uint8_t *buf, size_t size) {
    // stores can follow an alternate pattern:
    // movq %rX, %r11
    // andq %r15, %r11
    // mov <anything> (%r14, %r11)
    // The rewriter likes to do this for
    // addq/andq/xorq/etc. (grrr cisc)

    bool storesonly = v->opts->box == LFI_BOX_STORES;
    FdInstr i_mov, i_and, i_store;
    size_t offset = 0;
    if (fd_decode(&buf[offset], size, 64, 0, &i_mov) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_mov.size;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_and) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_and.size;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_store) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_store.size;

    //this should be safe, since we still check for AND mask
    if ((FD_TYPE(&i_mov) != FDI_MOV && 
            FD_TYPE(&i_mov) != FDI_LEA) ||
            FD_OP_TYPE(&i_mov, 0) != FD_OT_REG ||
            (!storesonly && FD_OP_TYPE(&i_mov, 1) != FD_OT_REG) ||
            FD_OP_REG(&i_mov, 0) != FD_REG_R11)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_and) != FDI_AND ||
            FD_OP_TYPE(&i_and, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_and, 1) != FD_OT_REG ||
            FD_OP_REG(&i_and, 0) != FD_REG_R11 ||
            FD_OP_REG(&i_and, 1) != FD_REG_R15)
        return (struct MacroInst){-1, 0};
    //we actually don't really care if the 
    //store instruction is a mov. We just care
    //that it only writes to the memory address specified
    //in operand 0
    if(FD_TYPE(&i_store) == FDI_XCHG ||
        FD_TYPE(&i_store) == FDI_CMPXCHG) {
        if((FD_OP_TYPE(&i_store, 0) == FD_OT_MEM &&
            check_unsafe_store(&i_store, 0)) ||
            (FD_OP_TYPE(&i_store, 1) == FD_OT_MEM &&
                check_unsafe_store(&i_store, 1))
           )
            return (struct MacroInst){-1, 0};
    } else {
        if (FD_OP_TYPE(&i_store, 0) != FD_OT_MEM ||
            check_unsafe_store(&i_store, 0))
            return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, 3};
}

static struct MacroInst macroinst_store_two(struct Verifier *v, uint8_t *buf, size_t size) {
    //stores sometimes also follow this pattern:
    // andq %r15, %r11
    // mov <anything> (%r14, %r11)

    bool storesonly = v->opts->box == LFI_BOX_STORES;
    FdInstr i_and, i_store;
    size_t offset = 0;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_and) < 0)
        return (struct MacroInst){-1, 0};
    offset += i_and.size;
    if (fd_decode(&buf[offset], size - offset, 64, 0, &i_store) < 0) {
        return (struct MacroInst){-1, 0};
    }
    offset += i_store.size;

    if (FD_TYPE(&i_and) != FDI_AND ||
            FD_OP_TYPE(&i_and, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_and, 1) != FD_OT_REG ||
            FD_OP_REG(&i_and, 0) != FD_REG_R11 ||
            FD_OP_REG(&i_and, 1) != FD_REG_R15)
        return (struct MacroInst){-1, 0};
    //we actually don't really care if the 
    //store instruction is a mov. We just care
    //that it only writes to the memory address specified
    //in operand 0
    if(FD_TYPE(&i_store) == FDI_XCHG ||
        FD_TYPE(&i_store) == FDI_CMPXCHG) {
        if((FD_OP_TYPE(&i_store, 0) == FD_OT_MEM &&
            check_unsafe_store(&i_store, 0)) ||
            (FD_OP_TYPE(&i_store, 1) == FD_OT_MEM &&
                check_unsafe_store(&i_store, 1))
           )
            return (struct MacroInst){-1, 0};
    } else {
        if (FD_OP_TYPE(&i_store, 0) != FD_OT_MEM ||
            check_unsafe_store(&i_store, 0))
            return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, 2};
}

static struct MacroInst macroinst_load(struct Verifier *v, uint8_t *buf, size_t size) {
    // TODO: another one of those instructions where
    // the actual address might be moved into r11 outside the bundle
    // we sadly cannot reserve r11, so this will have to change
    // pext %r15, %rX, %r11
    // movq (%r14, %r11), %rX

    FdInstr i_pext, i_load;
    if (fd_decode(&buf[0], size, 64, 0, &i_pext) < 0)
        return (struct MacroInst){-1, 0};
    if (fd_decode(&buf[i_pext.size], size - i_pext.size, 64, 0, &i_load) < 0)
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_pext) != FDI_PEXT ||
            FD_OP_TYPE(&i_pext, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_pext, 1) != FD_OT_REG ||
            FD_OP_TYPE(&i_pext, 2) != FD_OT_REG ||
            FD_OP_REG(&i_pext, 2) != FD_REG_R15 ||
            FD_OP_REG(&i_pext, 0) != FD_REG_R11
        )
        return (struct MacroInst){-1, 0};

    if (FD_TYPE(&i_load) != FDI_MOV ||
            FD_OP_TYPE(&i_load, 0) != FD_OT_REG ||
            reserved(&i_load, 0) ||
            FD_OP_TYPE(&i_load, 1) != FD_OT_MEM ||
            FD_OP_BASE(&i_load, 1) != FD_REG_R14 ||
            FD_OP_INDEX(&i_load, 1) != FD_OP_REG(&i_pext, 0) ||
            FD_OP_SCALE(&i_load, 1) != 0 ||
            FD_OP_DISP(&i_load, 1) > GUARD_SZ ||
            FD_OP_DISP(&i_load, 1) < -GUARD_SZ)
        return (struct MacroInst){-1, 0};

    return (struct MacroInst){i_pext.size + i_load.size, 2};
}

static struct MacroInst macroinst_modsp(struct Verifier *v, uint8_t *buf, size_t size) {
    // mov/sub/add/and ..., %rsp
    // andq %r15, %rsp
    // orq %r14, %rsp
    //
    // there is also a special case for lea:
    // leaq (...), %rsp
    // pext %r15, %rsp, %rsp
    // leaq (%rsp, %r14), %rsp
    //
    // and for small integers:
    // andq %r15, %rsp
    // leaq <offset>(%rsp, %r14), %rsp

    FdInstr i_mov, i_and, i_or;
    size_t count = 0;
    if (fd_decode(&buf[0], size, 64, 0, &i_mov) < 0)
        return (struct MacroInst){-1, 0};
    count += i_mov.size;

    if (fd_decode(&buf[count], size - count, 64, 0, &i_and) < 0)
        return (struct MacroInst){-1, 0};
    count += i_and.size;
    //we handle the case for small numbers now
    if(FD_TYPE(&i_mov) == FDI_AND &&
        assert_reg(&i_mov, 0, FD_REG_SP, 8) &&
        assert_reg(&i_mov, 1, FD_REG_R15, 8) &&
        //assert that it's lea
        FD_TYPE(&i_and) == FDI_LEA &&
        assert_reg(&i_and, 0, FD_REG_SP, 8) &&
        FD_OP_TYPE(&i_and, 1) == FD_OT_MEM &&
        FD_OP_BASE(&i_and, 1) == FD_REG_SP &&
        FD_OP_INDEX(&i_and, 1) == FD_REG_R14 &&
        FD_OP_SCALE(&i_and, 1) == 0 &&
        FD_OP_DISP(&i_and, 1) < GUARD_SZ &&
        FD_OP_DISP(&i_and, 1) > -GUARD_SZ) {
        return (struct MacroInst){count, 2};
    }
    if (fd_decode(&buf[count], size - count, 64, 0, &i_or) < 0)
        return (struct MacroInst){-1, 0};
    count += i_or.size;

    // allow addl, subl, lea, add movl
    if (FD_TYPE(&i_mov) != FDI_MOV &&
            FD_TYPE(&i_mov) != FDI_ADD &&
            FD_TYPE(&i_mov) != FDI_AND &&
            FD_TYPE(&i_mov) != FDI_SUB &&
            FD_TYPE(&i_mov) != FDI_LEA)
        return (struct MacroInst){-1, 0};

    if (FD_OP_TYPE(&i_mov, 0) != FD_OT_REG ||
            FD_OP_SIZE(&i_mov, 0) != 8 ||
            FD_OP_REG(&i_mov, 0) != FD_REG_SP)
        return (struct MacroInst){-1, 0};
    if(FD_TYPE(&i_mov) != FDI_LEA) {
        if(FD_TYPE(&i_and) != FDI_AND ||
            !assert_reg(&i_and, 0, FD_REG_SP, 8) ||
            !assert_reg(&i_and, 1, FD_REG_R15, 8))
            return (struct MacroInst){-1, 0};
        if(FD_TYPE(&i_or) != FDI_OR ||
            !assert_reg(&i_or, 0, FD_REG_SP, 8) ||
            !assert_reg(&i_or, 1, FD_REG_R14, 8))
            return (struct MacroInst){-1, 0};
    } else {
        if(FD_TYPE(&i_and) != FDI_PEXT ||
                !assert_reg(&i_and, 0, FD_REG_SP, 8) ||
                !assert_reg(&i_and, 1, FD_REG_SP, 8) ||
                !assert_reg(&i_and, 2, FD_REG_R15, 8))
            return (struct MacroInst){-1, 0};

        if(FD_TYPE(&i_or) != FDI_LEA ||
                !assert_reg(&i_or, 0, FD_REG_SP, 8) ||
                FD_OP_TYPE(&i_or, 1) != FD_OT_MEM ||
                FD_OP_BASE(&i_or, 1) != FD_REG_SP ||
                FD_OP_INDEX(&i_or, 1) != FD_REG_R14 ||
                FD_OP_DISP(&i_or, 1) != 0 ||
                FD_OP_SCALE(&i_or, 1) != 0)
            return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){count, 3};
}


typedef struct MacroInst (*MacroFn)(struct Verifier *, uint8_t*, size_t);

static MacroFn mfns[] = {
    macroinst_jmp,
    macroinst_call,
    macroinst_rtcall,
    macroinst_modsp,
    macroinst_stos,
    macroinst_movs,
    macroinst_load,
    macroinst_store_pext,
    macroinst_store_and,
    macroinst_store_two
};

static struct MacroInst macroinst(struct Verifier *v, uint8_t *buf, size_t size) {
    for (size_t i = 0; i < sizeof(mfns) / sizeof(mfns[0]); i++) {
        struct MacroInst mi = mfns[i](v, buf, size);
        if (mi.size > 0)
            return mi;
    }
    return (struct MacroInst){-1, 0};
}
