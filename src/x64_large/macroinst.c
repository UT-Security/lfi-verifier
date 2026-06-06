struct MacroInst {
    int size;
    int ninstr;
};

static bool assert_reg(FdInstr *instr, uint8_t op_index, FdReg reg, uint8_t op_size) {
    return (FD_OP_TYPE(instr, op_index) == FD_OT_REG &&
        FD_OP_REG(instr, op_index) == reg &&
        FD_OP_SIZE(instr, op_index) == op_size);
}

static struct MacroInst macroinst_stos(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // stos becomes:
    //
    // pext %r15 %rdi, %rdi
    // leaq (%r14, %rdi), %rdi
    // rep stosq
    FdInstr *i_pext = &bundle->instrs[idx];

    FdInstr i_lea, i_stos;
    size_t offset = 0;

    offset += i_pext->size;
    if (FD_TYPE(i_pext) != FDI_PEXT ||
        !assert_reg(i_pext, 0, FD_REG_DI, 8) ||
        !assert_reg(i_pext, 1, FD_REG_DI, 8) ||
        !assert_reg(i_pext, 2, FD_REG_R15, 8)) {
        return (struct MacroInst){-1, 0};
    }

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_lea = bundle->instrs[idx + 1];

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

    if (!bundle->valid[idx + 2])
        return (struct MacroInst){-1, 0};
    i_stos = bundle->instrs[idx + 2];
    offset += i_stos.size;
    if (FD_TYPE(&i_stos) != FDI_STOS) {
        return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, 3};
}

static struct MacroInst macroinst_movs(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // movs becomes:
    // pext %r15, %rdi, %rdi
    // leaq (%r14, %rdi), %rdi
    // pext %r15, %rsi, %rsi
    // leaq (%r14, %rsi), %rsi
    // movsq

    FdInstr *i_pext = &bundle->instrs[idx];

    FdInstr i_lea, i_pext2, i_lea2, i_movs;
    size_t offset = 0;
    size_t icount = 0;
    bool storesonly = v->opts->box == LFI_BOX_STORES;

    offset += i_pext->size;
    icount++;
    if (FD_TYPE(i_pext) != FDI_PEXT ||
        !assert_reg(i_pext, 0, FD_REG_DI, 8) ||
        !assert_reg(i_pext, 1, FD_REG_DI, 8) ||
        !assert_reg(i_pext, 2, FD_REG_R15, 8)) {
        return (struct MacroInst){-1, 0};
    }

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_lea = bundle->instrs[idx + 1];
    offset += i_lea.size;
    icount++;
    if (FD_TYPE(&i_lea) != FDI_LEA ||
        !assert_reg(&i_lea, 0, FD_REG_DI, 8) ||
        FD_OP_TYPE(&i_lea, 1) != FD_OT_MEM ||
        FD_OP_BASE(&i_lea, 1) != FD_REG_R14 ||
        FD_OP_INDEX(&i_lea, 1) != FD_REG_DI ||
        FD_OP_DISP(&i_lea, 1) != 0 ||
        FD_OP_SCALE(&i_lea, 1) != 0){
        return (struct MacroInst){-1, 0};
    }

    if(!storesonly) {
        if (!bundle->valid[idx + 2])
            return (struct MacroInst){-1, 0};
        i_pext2 = bundle->instrs[idx + 2];

        offset += i_pext2.size;
        icount++;
        if (FD_TYPE(&i_pext2) != FDI_PEXT ||
            !assert_reg(&i_pext2, 0, FD_REG_SI, 8) ||
            !assert_reg(&i_pext2, 1, FD_REG_SI, 8) ||
            !assert_reg(&i_pext2, 2, FD_REG_R15, 8)) {
            return (struct MacroInst){-1, 0};
        }

        if (!bundle->valid[idx + 3])
            return (struct MacroInst){-1, 0};
        i_lea2 = bundle->instrs[idx + 3];
        offset += i_lea2.size;
        icount++;
        if (FD_TYPE(&i_lea2) != FDI_LEA ||
            !assert_reg(&i_lea2, 0, FD_REG_SI, 8) ||
            FD_OP_TYPE(&i_lea2, 1) != FD_OT_MEM ||
            FD_OP_BASE(&i_lea2, 1) != FD_REG_R14 ||
            FD_OP_INDEX(&i_lea2, 1) != FD_REG_SI ||
            FD_OP_DISP(&i_lea2, 1) != 0 ||
            FD_OP_SCALE(&i_lea2, 1) != 0){
            return (struct MacroInst){-1, 0};
        }
    }

    uint32_t mov_idx = storesonly ? idx + 2 : idx + 4;
    if (!bundle->valid[mov_idx])
        return (struct MacroInst){-1, 0};
    i_movs = bundle->instrs[mov_idx];
    offset += i_movs.size;
    icount++;
    if (FD_TYPE(&i_movs) != FDI_MOVS) {
        return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, icount};
}

static struct MacroInst macroinst_jmp(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // andq %r15, %rX
    // andq $0xffffffffffffffe0, %rX
    // orq %r14, %rX
    // jmp *(%rX)

    FdInstr *i_and = &bundle->instrs[idx];

    FdInstr i_and2, i_or, i_jmp;
    size_t offset = 0;
    offset += i_and->size;

    if (FD_TYPE(i_and) != FDI_AND ||
            !assert_reg(i_and, 1, FD_REG_R15, 8) ||
            FD_OP_TYPE(i_and, 0) != FD_OT_REG ||
            reserved( v, i_and, 0) ||
            FD_OP_SIZE(i_and, 0) != 8)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_and2 = bundle->instrs[idx + 1];
    offset += i_and2.size;

    if (FD_TYPE(&i_and2) != FDI_AND ||
            FD_OP_TYPE(&i_and2, 0) != FD_OT_REG ||
            FD_OP_REG(&i_and2, 0) != FD_OP_REG(i_and, 0) ||
            FD_OP_SIZE(&i_and2, 0) != 8 ||
            FD_OP_TYPE(&i_and2, 1) != FD_OT_IMM ||
            FD_OP_IMM(&i_and2, 1) != 0xffffffffffffffe0)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 2])
        return (struct MacroInst){-1, 0};
    i_or = bundle->instrs[idx + 2];
    offset += i_or.size;

    if ((FD_TYPE(&i_or) != FDI_OR && FD_TYPE(&i_or) != FDI_ADD) ||
            FD_OP_TYPE(&i_or, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_or, 1) != FD_OT_REG ||
            !assert_reg(&i_or, 0, FD_OP_REG(&i_and2, 0), 8) ||
            !assert_reg(&i_or, 1, FD_REG_R14, 8))
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 3])
        return (struct MacroInst){-1, 0};
    i_jmp = bundle->instrs[idx + 3];
    offset += i_jmp.size;

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

static bool okrtcalldisp(int64_t disp) {
    switch (disp) {
    case 0:
    case 8:
    case 16:
    case 24:
    case -8:
    case -16:
    case -24:
    case -32:
        return true;
    }
    return false;
}

static struct MacroInst macroinst_rtcall(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // leaq 1f(%rip), %r11
    // jmpq *N(%r14)
    // 1:
    FdInstr *i_lea = &bundle->instrs[idx];

    FdInstr i_jmp;

    if (FD_TYPE(i_lea) != FDI_LEA ||
            FD_OP_TYPE(i_lea, 0) != FD_OT_REG ||
            FD_OP_REG(i_lea, 0) != FD_REG_R11 ||
            FD_OP_TYPE(i_lea, 1) != FD_OT_MEM ||
            FD_OP_BASE(i_lea, 1) != FD_REG_IP)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_jmp = bundle->instrs[idx + 1];

    if (FD_TYPE(&i_jmp) != FDI_JMP ||
            FD_OP_TYPE(&i_jmp, 0) != FD_OT_MEM ||
            FD_OP_BASE(&i_jmp, 0) != FD_REG_R14 ||
            FD_OP_INDEX(&i_jmp, 0) != FD_REG_NONE ||
            FD_OP_SCALE(&i_jmp, 0) != 0 ||
            !okrtcalldisp(FD_OP_DISP(&i_jmp, 0)))
        return (struct MacroInst){-1, 0};

    // Return target can either be the next instruction or can be some
    // bundle-aligned location.
    uintptr_t ret = v->addr + i_lea->size + FD_OP_DISP(i_lea, 1);
    bool ok = FD_OP_DISP(i_lea, 1) == i_jmp.size || ret % v->bundlesize == 0;
    if (!ok)
        return (struct MacroInst){-1, 0};

    return (struct MacroInst){i_lea->size + i_jmp.size, 2};
}

static struct MacroInst macroinst_call(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    size_t bundlesize = 32;

    // TODO: this relies on a movsq instruction outside of the bundle
    // andq %r15, %rX
    // andq $0xffffffffffffffe0, %rX
    // orq %r14, %rX
    // NOP*
    // callq *%rX

    FdInstr *i_and = &bundle->instrs[idx];

    FdInstr i_and2, i_or, i_jmp;
    size_t offset = 0;
    offset += i_and->size;

    if (FD_TYPE(i_and) != FDI_AND ||
            !assert_reg(i_and, 1, FD_REG_R15, 8) ||
            FD_OP_TYPE(i_and, 0) != FD_OT_REG ||
            reserved( v, i_and, 0) ||
            FD_OP_SIZE(i_and, 0) != 8)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_and2 = bundle->instrs[idx + 1];
    offset += i_and2.size;

    if (FD_TYPE(&i_and2) != FDI_AND ||
            FD_OP_TYPE(&i_and2, 0) != FD_OT_REG ||
            FD_OP_REG(&i_and2, 0) != FD_OP_REG(i_and, 0) ||
            FD_OP_SIZE(&i_and2, 0) != 8 ||
            FD_OP_TYPE(&i_and2, 1) != FD_OT_IMM ||
            FD_OP_IMM(&i_and2, 1) != 0xffffffffffffffe0)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 2])
        return (struct MacroInst){-1, 0};
    i_or = bundle->instrs[idx + 2];
    offset += i_or.size;

    if ((FD_TYPE(&i_or) != FDI_OR && FD_TYPE(&i_or) != FDI_ADD) ||
            FD_OP_TYPE(&i_or, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_or, 1) != FD_OT_REG ||
            !assert_reg(&i_or, 0, FD_OP_REG(&i_and2, 0), 8) ||
            !assert_reg(&i_or, 1, FD_REG_R14, 8))
        return (struct MacroInst){-1, 0};

    size_t icount = 3;
    while(offset < bundlesize) {
        if (!bundle->valid[icount])
            return (struct MacroInst){-1, 0};
        i_jmp = bundle->instrs[idx + icount];
        offset += i_jmp.size;
        icount++;
        if (FD_TYPE(&i_jmp) != FDI_NOP)
            break;
    }

    if (FD_TYPE(&i_jmp) != FDI_CALL ||
            FD_OP_TYPE(&i_jmp, 0) != FD_OT_REG ||
            FD_OP_REG(&i_jmp, 0) != FD_OP_REG(&i_and2, 0)
        ) {
        return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, icount};
}

static struct MacroInst macroinst_hlt(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // for spidermonkey, data will sometimes be stored in code section
    // we solve that by beginning each bundle of data with an hlt instruction
    // this is safe since we have that all jumps are bundle-aligned, or we'll manually
    // check that the jump target is safe
    // hlt
    // anything you want
    FdInstr *i_halt = &bundle->instrs[idx];

    FdInstr i_any;
    int64_t bundlesize = v->bundlesize;
    size_t count = 0;
    size_t icount = 1;

    if(FD_TYPE(i_halt) != FDI_HLT)
        return (struct MacroInst){-1, 0};

    count = bundlesize - (v->addr % bundlesize);
    // we just skip over the rest of the instructions, up to size
    for (size_t i = idx + 1; i < bundle->size; i++) {
        count += bundle->instrs[i].size;
        icount++;
    }
    return (struct MacroInst){count, icount};
}

bool check_unsafe_store(FdInstr* inst, uint32_t op, uint32_t reg, int64_t guard) {
    if(FD_OP_BASE(inst, op) == FD_REG_R14 && FD_OP_INDEX(inst, op) == FD_REG_NONE &&
        FD_OP_SCALE(inst, op) == 0 && FD_OP_DISP(inst, op) > guard && FD_OP_DISP(inst, op) < -guard)
        return false;
    return
        FD_OP_BASE(inst, op) != FD_REG_R14 ||
        FD_OP_INDEX(inst, op) != reg ||
        FD_OP_SCALE(inst, op) != 0 ||
        FD_OP_DISP(inst, op) > guard ||
        FD_OP_DISP(inst, op) < -guard;
}

bool unsafe_store_any(FdInstr* inst, uint32_t op, uint32_t reg, int64_t guard) {
    return
        FD_OP_BASE(inst, op) != reg ||
        FD_OP_INDEX(inst, op) != FD_REG_NONE ||
        FD_OP_SCALE(inst, op) != 0 ||
        FD_OP_DISP(inst, op) > guard ||
        FD_OP_DISP(inst, op) < -guard;
}

static struct MacroInst macroinst_store_pext(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // note: for  SIB, the actual address will be moved
    // into r11 beforehand
    // this will not work if we cannot reserve r11
    // pext %r15, %rX, %rY
    // mov %rX, (%r14, %rY)

    FdInstr *i_pext = &bundle->instrs[idx];

    FdInstr i_store;
    int64_t  guardsize = v->opts->guardsize;

    if (FD_TYPE(i_pext) != FDI_PEXT ||
            FD_OP_TYPE(i_pext, 0) != FD_OT_REG ||
            FD_OP_TYPE(i_pext, 1) != FD_OT_REG ||
            FD_OP_TYPE(i_pext, 2) != FD_OT_REG ||
            FD_OP_REG(i_pext, 2) != FD_REG_R15 ||
            reserved( v, i_pext, 0)
        )
        return (struct MacroInst){-1, 0};

    uint32_t targ = FD_OP_REG(i_pext, 0);
    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_store = bundle->instrs[idx + 1];

    if(FD_TYPE(&i_store) == FDI_XCHG ||
        FD_TYPE(&i_store) == FDI_CMPXCHG) {
        if((FD_OP_TYPE(&i_store, 0) == FD_OT_MEM &&
            check_unsafe_store(&i_store, 0, targ ,guardsize)) ||
            (FD_OP_TYPE(&i_store, 1) == FD_OT_MEM &&
                check_unsafe_store(&i_store, 1, targ, guardsize))
           )
            return (struct MacroInst){-1, 0};
    } else {
        if (FD_OP_TYPE(&i_store, 0) != FD_OT_MEM ||
            check_unsafe_store(&i_store, 0, targ, guardsize))
            return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){i_pext->size + i_store.size, 2};
}

static struct MacroInst macroinst_store_pext_multi(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // pext %r15, %rX, %rX
    // lea (%r14, %rX), %rX
    // mov rX, rX

    FdInstr *i_pext = &bundle->instrs[idx];

    FdInstr i_store, i_lea;
    uint32_t offset = 0;
    offset += i_pext->size;

    if (FD_TYPE(i_pext) != FDI_PEXT ||
            FD_OP_TYPE(i_pext, 0) != FD_OT_REG ||
            FD_OP_TYPE(i_pext, 1) != FD_OT_REG ||
            FD_OP_TYPE(i_pext, 2) != FD_OT_REG ||
            FD_OP_REG(i_pext, 2) != FD_REG_R15
        )
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_lea = bundle->instrs[idx + 1];
    offset += i_lea.size;

    if (FD_TYPE(&i_lea) != FDI_LEA ||
            FD_OP_TYPE(&i_lea, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_lea, 1) != FD_OT_MEM ||
            FD_OP_BASE(&i_lea, 1) != FD_REG_R14 ||
            FD_OP_INDEX(&i_lea, 1) != FD_OP_REG(i_pext, 0) ||
            FD_OP_SCALE(&i_lea, 1) != 0 ||
            FD_OP_DISP(&i_lea, 1) != 0)
        return (struct MacroInst){-1, 0};

    int64_t guardsize = v->opts->guardsize;
    if (!bundle->valid[idx + 2])
        return (struct MacroInst){-1, 0};
    i_store = bundle->instrs[idx + 2];
    offset += i_store.size;

    if (FD_TYPE(&i_store) != FDI_MOV ||
            FD_OP_TYPE(&i_store, 0) != FD_OT_MEM ||
            FD_OP_BASE(&i_store, 0) != FD_OP_REG(&i_lea, 0) ||
            FD_OP_SCALE(&i_store, 0) != 0 ||
            FD_OP_DISP(&i_store, 0) > guardsize ||
            FD_OP_DISP(&i_store, 0) < -guardsize)
        return (struct MacroInst){-1, 0};

    return (struct MacroInst){offset, 3};
}

static struct MacroInst macroinst_store_three(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // andq %r15, %rX
    // or %r14, %rX
    // mov <anything>, (%rX, off)

    FdInstr *i_and = &bundle->instrs[idx];
    
    bool storesonly = v->opts->box == LFI_BOX_STORES;
    int64_t guardsize = v->opts->guardsize;
    FdInstr i_or, i_store;
    size_t offset = 0;
    offset += i_and->size;

    if (FD_TYPE(i_and) != FDI_AND ||
            FD_OP_TYPE(i_and, 0) != FD_OT_REG ||
            FD_OP_TYPE(i_and, 1) != FD_OT_REG ||
            reserved( v, i_and, 0) || 
            FD_OP_REG(i_and, 1) != FD_REG_R15)
        return (struct MacroInst){-1, 0};

    uint32_t targ = FD_OP_REG(i_and, 0);
    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_or = bundle->instrs[idx + 1];
    offset += i_or.size;

    if (FD_TYPE(&i_or) != FDI_OR ||
            FD_OP_TYPE(&i_or, 0) != FD_OT_REG ||
            FD_OP_TYPE(&i_or, 1) != FD_OT_REG ||
            FD_OP_REG(&i_or, 0) != targ ||
            FD_OP_REG(&i_or, 1) != FD_REG_R14)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 2])
        return (struct MacroInst){-1, 0};
    i_store = bundle->instrs[idx + 2];
    offset += i_store.size;
    //we actually don't really care if the 
    //store instruction is a mov. We just care
    //that it only writes to the memory address specified
    //in operand 0
    if(FD_TYPE(&i_store) == FDI_XCHG ||
        FD_TYPE(&i_store) == FDI_CMPXCHG) {
        if((FD_OP_TYPE(&i_store, 0) == FD_OT_MEM &&
            unsafe_store_any(&i_store, 0, targ, guardsize)) ||
            (FD_OP_TYPE(&i_store, 1) == FD_OT_MEM &&
                unsafe_store_any(&i_store, 1, targ, guardsize))
           )
            return (struct MacroInst){-1, 0};
    } else {
        if (FD_OP_TYPE(&i_store, 0) != FD_OT_MEM ||
            unsafe_store_any(&i_store, 0, targ, guardsize))
            return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, 3};
}

static struct MacroInst macroinst_store_two(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    //stores sometimes also follow this pattern:
    // andq %r15, %r11
    // mov <anything> (%r14, %r11)

    FdInstr *i_and = &bundle->instrs[idx];

    bool storesonly = v->opts->box == LFI_BOX_STORES;
    int64_t guardsize = v->opts->guardsize;
    FdInstr i_store;
    size_t offset = 0;
    offset += i_and->size;

    if (FD_TYPE(i_and) != FDI_AND ||
            FD_OP_TYPE(i_and, 0) != FD_OT_REG ||
            FD_OP_TYPE(i_and, 1) != FD_OT_REG ||
            reserved( v, i_and, 0) || 
            FD_OP_REG(i_and, 1) != FD_REG_R15)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_store = bundle->instrs[idx + 1];
    offset += i_store.size;

    uint32_t targ = FD_OP_REG(i_and, 0);
    //we actually don't really care if the 
    //store instruction is a mov. We just care
    //that it only writes to the memory address specified
    //in operand 0
    if(FD_TYPE(&i_store) == FDI_XCHG ||
        FD_TYPE(&i_store) == FDI_CMPXCHG) {
        if((FD_OP_TYPE(&i_store, 0) == FD_OT_MEM &&
            check_unsafe_store(&i_store, 0, targ, guardsize)) ||
            (FD_OP_TYPE(&i_store, 1) == FD_OT_MEM &&
                check_unsafe_store(&i_store, 1, targ, guardsize))
           )
            return (struct MacroInst){-1, 0};
    } else {
        if (FD_OP_TYPE(&i_store, 0) != FD_OT_MEM ||
            check_unsafe_store(&i_store, 0, targ, guardsize))
            return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, 2};
}


static struct MacroInst macroinst_load(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // pext %r15, %rX, %r11
    // movq (%r14, %r11), %rX
    int64_t guardsize = v->opts->guardsize;

    FdInstr *i_pext = &bundle->instrs[idx];

    FdInstr i_load;

    if (FD_TYPE(i_pext) != FDI_PEXT ||
            FD_OP_TYPE(i_pext, 0) != FD_OT_REG ||
            FD_OP_TYPE(i_pext, 1) != FD_OT_REG ||
            FD_OP_TYPE(i_pext, 2) != FD_OT_REG ||
            FD_OP_REG(i_pext, 2) != FD_REG_R15 ||
            reserved( v, i_pext, 0)
        )
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_load = bundle->instrs[idx + 1];

    if (FD_TYPE(&i_load) != FDI_MOV ||
            FD_OP_TYPE(&i_load, 0) != FD_OT_REG ||
            reserved( v, &i_load, 0) ||
            FD_OP_TYPE(&i_load, 1) != FD_OT_MEM ||
            FD_OP_BASE(&i_load, 1) != FD_REG_R14 ||
            FD_OP_INDEX(&i_load, 1) != FD_OP_REG(i_pext, 0) ||
            FD_OP_SCALE(&i_load, 1) != 0 ||
            FD_OP_DISP(&i_load, 1) > guardsize ||
            FD_OP_DISP(&i_load, 1) < -guardsize)
        return (struct MacroInst){-1, 0};

    return (struct MacroInst){i_pext->size + i_load.size, 2};
}

static struct MacroInst macroinst_load_two(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    //stores sometimes also follow this pattern:
    // andq %r15, %r11
    // mov <anything> (%r14, %r11)

    FdInstr *i_and = &bundle->instrs[idx];

    bool storesonly = v->opts->box == LFI_BOX_STORES;
    int64_t guardsize = v->opts->guardsize;
    FdInstr i_store;
    size_t offset = 0;
    offset += i_and->size;

    if (FD_TYPE(i_and) != FDI_AND ||
            FD_OP_TYPE(i_and, 0) != FD_OT_REG ||
            FD_OP_TYPE(i_and, 1) != FD_OT_REG ||
            reserved( v, i_and, 0) || 
            FD_OP_REG(i_and, 1) != FD_REG_R15)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_store = bundle->instrs[idx + 1];
    offset += i_store.size;

    uint32_t targ = FD_OP_REG(i_and, 0);
    //we actually don't really care if the 
    //store instruction is a mov. We just care
    //that it only writes to the memory address specified
    //in operand 0
    if(FD_TYPE(&i_store) == FDI_XCHG ||
        FD_TYPE(&i_store) == FDI_CMPXCHG) {
        if((FD_OP_TYPE(&i_store, 0) == FD_OT_MEM &&
            check_unsafe_store(&i_store, 0, targ, guardsize)) ||
            (FD_OP_TYPE(&i_store, 1) == FD_OT_MEM &&
                check_unsafe_store(&i_store, 1, targ, guardsize))
           )
            return (struct MacroInst){-1, 0};
    } else {
        if (FD_OP_TYPE(&i_store, 1) != FD_OT_MEM ||
            check_unsafe_store(&i_store, 1, targ, guardsize))
            return (struct MacroInst){-1, 0};
    }

    return (struct MacroInst){offset, 2};
}

static struct MacroInst macroinst_modsp(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
    // mov/sub/add/and ..., %rsp
    // andq %r15, %rsp
    // ~~orq %r14, %rsp~~
    //
    // there is also a special case for lea:
    // leaq (...), %rsp
    // pext %r15, %rsp, %rsp
    // leaq (%rsp, %r14), %rsp
    //
    // and for small integers:
    // andq %r15, %rsp
    // leaq <offset>(%rsp, %r14), %rsp

    FdInstr *i_mov = &bundle->instrs[idx];

    int64_t guardsize = v->opts->guardsize;
    FdInstr i_and, i_or;
    size_t count = 0;
    bool storesonly = v->opts->box == LFI_BOX_STORES;

    count += i_mov->size;

    // allow addl, subl, lea, add movl, pop
    if (FD_TYPE(i_mov) != FDI_MOV &&
            FD_TYPE(i_mov) != FDI_ADD &&
            FD_TYPE(i_mov) != FDI_AND &&
            FD_TYPE(i_mov) != FDI_SUB &&
            FD_TYPE(i_mov) != FDI_LEA &&
            FD_TYPE(i_mov) != FDI_POP)
        return (struct MacroInst){-1, 0};

    if (!bundle->valid[idx + 1])
        return (struct MacroInst){-1, 0};
    i_and = bundle->instrs[idx + 1];
    count += i_and.size;
    //we handle the case for small numbers now
    if(FD_TYPE(i_mov) == FDI_AND &&
        assert_reg(i_mov, 0, FD_REG_SP, 8) &&
        assert_reg(i_mov, 1, FD_REG_R15, 8) &&
        //assert that it's lea
        FD_TYPE(&i_and) == FDI_LEA &&
        assert_reg(&i_and, 0, FD_REG_SP, 8) &&
        FD_OP_TYPE(&i_and, 1) == FD_OT_MEM &&
        FD_OP_BASE(&i_and, 1) == FD_REG_SP &&
        FD_OP_INDEX(&i_and, 1) == FD_REG_R14 &&
        FD_OP_SCALE(&i_and, 1) == 0 &&
        FD_OP_DISP(&i_and, 1) < guardsize &&
        FD_OP_DISP(&i_and, 1) > -guardsize
        ) {
        return (struct MacroInst){count, 2};
    }
    if (!bundle->valid[idx + 2])
        return (struct MacroInst){-1, 0};
    i_or = bundle->instrs[idx + 2];
    count += i_or.size;

    //TODO: this does not work for read sandbox
    if (FD_OP_TYPE(i_mov, 0) != FD_OT_REG ||
            FD_OP_SIZE(i_mov, 0) != 8 ||
            FD_OP_REG(i_mov, 0) != FD_REG_SP)
        return (struct MacroInst){-1, 0};

    if(FD_TYPE(&i_and) == FDI_AND &&
        assert_reg(&i_and, 0, FD_REG_SP, 8) &&
        assert_reg(&i_and, 1, FD_REG_R15, 8) &&
        (FD_TYPE(&i_or) == FDI_LEA || FD_TYPE(&i_or) == FDI_ADD
         || FD_TYPE(&i_or) == FDI_OR) &&
        assert_reg(&i_or, 0, FD_REG_SP, 8) &&
        assert_reg(&i_or, 1, FD_REG_R14, 8))
        return (struct MacroInst){count, 3};

    // if(FD_TYPE(i_mov) != FDI_LEA)
    //     return (struct MacroInst){-1, 0};

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

    return (struct MacroInst){count, 3};
}


typedef struct MacroInst (*MacroFn)(struct Verifier *, FdInstrBundle *, size_t);

static MacroFn mfns[] = {
    macroinst_store_pext,
    macroinst_store_two,
    macroinst_store_three,
    macroinst_store_pext_multi,
    macroinst_load,
    macroinst_load_two,
    macroinst_jmp,
    macroinst_call,
    macroinst_rtcall,
    macroinst_modsp,
    macroinst_stos,
    macroinst_movs,
    macroinst_hlt
};

static struct MacroInst macroinst(struct Verifier *v, FdInstrBundle *bundle, size_t idx) {
#define MACROINST(FN)             \
    mi = FN(v, bundle, idx); \
    if (mi.size > 0)              \
        return mi;

    struct MacroInst mi;

    MACROINST(macroinst_store_two);
    MACROINST(macroinst_store_pext);
    MACROINST(macroinst_store_three);
    MACROINST(macroinst_store_pext_multi);
    MACROINST(macroinst_load_two);
    MACROINST(macroinst_jmp);
    MACROINST(macroinst_call);
    MACROINST(macroinst_rtcall);
    MACROINST(macroinst_modsp);
    MACROINST(macroinst_stos);
    MACROINST(macroinst_movs);
    //MACROINST(macroinst_hlt);

    return (struct MacroInst){-1, 0};
}
