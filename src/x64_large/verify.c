#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lfiv.h"
#include "fadec.h"

extern uint8_t lfi_x86_bdd(uint8_t *);

struct Verifier {
    bool failed;
    bool abort;
    uintptr_t addr;
    uintptr_t base;
    struct LFIVOptions *opts;
    size_t bundlesize;
};

enum {
    ERRMAX = 128,
};

static void verrmin(struct Verifier *v, const char* fmt, ...) {
    v->failed = true;

    if (!v->opts->err)
        return;

    va_list ap;

    char errbuf[ERRMAX];

    va_start(ap, fmt);
    vsnprintf(errbuf, ERRMAX, fmt, ap);
    va_end(ap);

    v->opts->err(errbuf, strlen(errbuf));
}

static void verr(struct Verifier *v, FdInstr *inst, const char* msg) {
    char fmtbuf[64];
    fd_format(inst, fmtbuf, sizeof(fmtbuf));
    verrmin(v, "%x: %s: %s", v->addr, fmtbuf, msg);
}

static int nmod(FdInstr *instr) {
    switch (FD_TYPE(instr)) {
    case FDI_CMP:
    case FDI_TEST:
        return 0;
    case FDI_XCHG:
        return 2;
    default:
        return 1;
    }
}

// Check whether a particular register is reserved under the current
// verification configuration.
// Accounts for reads vs writes.
static bool reserved(FdInstr *instr, int op_index) {
    // Allow all vector registers.
    if (FD_OP_REG_TYPE(instr, op_index) == FD_RT_VEC)
        return false;
    // Allow all FPU registers.
    if (FD_OP_REG_TYPE(instr, op_index) == FD_RT_FPU)
        return false;
    // Disallow anything else that is not GPR.
    if (FD_OP_REG_TYPE(instr, op_index) != FD_RT_GPL &&
        FD_OP_REG_TYPE(instr, op_index) != FD_RT_GPH)
        return true;

    FdReg reg = FD_OP_REG(instr, op_index);
    bool is_read_op = nmod(instr) <= op_index;

    if (reg == FD_REG_R14 || reg == FD_REG_SP || reg == FD_REG_R15 ) {
        return !is_read_op;
    }

    return false;
}

static bool branchinfo(struct Verifier *v, FdInstr *instr, int64_t *target, bool *indirect, bool *cond) {
    *target = 0;
    *indirect = false;
    *cond = false;

    // TODO: don't count runtime calls as branches

    bool branch = true;
    switch (FD_TYPE(instr)) {
    case FDI_JA:
    case FDI_JBE:
    case FDI_JC:
    case FDI_JCXZ:
    case FDI_JG:
    case FDI_JGE:
    case FDI_JL:
    case FDI_JLE:
    case FDI_JNC:
    case FDI_JNO:
    case FDI_JNP:
    case FDI_JNS:
    case FDI_JNZ:
    case FDI_JO:
    case FDI_JP:
    case FDI_JS:
    case FDI_JZ:
        *cond = true;
    case FDI_JMP:
    case FDI_CALL:
    case FDI_RET:
        if (FD_OP_TYPE(instr, 0) == FD_OT_OFF) {
            int64_t imm = FD_OP_IMM(instr, 0);
            *target = v->addr + FD_SIZE(instr) + imm;
        } else {
            *indirect = true;
        }
        break;
    default:
        branch = false;
        break;
    }
    return branch;
}

static bool okmnem(struct Verifier *v, FdInstr *instr) {
    switch (FD_TYPE(instr)) {
#include "base.instrs"
    default:
        return false;
    }
}

static void chkmem(struct Verifier *v, FdInstr *instr) {
    if (FD_TYPE(instr) == FDI_LEA || FD_TYPE(instr) == FDI_NOP)
        return;

    bool storesonly = v->opts->box == LFI_BOX_STORES;
    for (size_t i = 0; i < 4; i++) {
        if (FD_OP_TYPE(instr, i) == FD_OT_MEM) {
            if (FD_SEGMENT(instr) == FD_REG_GS) {
                if(storesonly && i != 0 && FD_TYPE(instr) != FDI_XCHG && FD_TYPE(instr) != FDI_CMPXCHG)
                    continue;
                if (FD_OP_DISP(instr, i) != 0 ||
                    FD_OP_BASE(instr, i) != FD_REG_NONE ||
                    FD_OP_INDEX(instr, i) != FD_REG_NONE)
                    verr(v, instr, "Can only access base of segmented memory");
                continue;
            } else if (FD_SEGMENT(instr) == FD_REG_FS) {
                verr(v, instr, "use of %%fs is not permitted");
                continue;
            }

            if (FD_ADDRSIZE(instr) != 8)
                verr(v, instr, "non-segmented memory access must use 64-bit address");
            //As far as I can tell, xchg is the only instruction
            //that clobbers memory without listing an address
            //as its first/destination operand
            if(storesonly && i != 0 && FD_TYPE(instr) != FDI_XCHG)
                continue;
            if(storesonly && 
                (FD_TYPE(instr) == FDI_CMP ||
                 FD_TYPE(instr) == FDI_TEST)
                )
                continue;
            if (FD_OP_BASE(instr, i) != FD_REG_SP &&
                    FD_OP_BASE(instr, i) != FD_REG_IP)
                verr(v, instr, "invalid base register for memory access");
            if (FD_OP_INDEX(instr, i) != FD_REG_NONE)
                verr(v, instr, "invalid index register for memory access");
        }
    }
}

static void chkmod(struct Verifier *v, FdInstr *instr) {
    if (FD_TYPE(instr) == FDI_NOP)
        return;

    for (size_t i = 0; i < 4; i++) {
        if (FD_OP_TYPE(instr, i) == FD_OT_REG && reserved(instr, i))
            verr(v, instr, "modification of reserved register");
    }
}

static size_t vchkbundle(struct Verifier *v, uint8_t* buf, size_t size);

static bool branchto(struct Verifier *v, int64_t target, FdInstr* insn) {
    int64_t i_target;
    bool indirect, cond;
    if(branchinfo(v, insn, &i_target, &indirect, &cond)) {
        if(!indirect && (i_target == target))
            return true;
    }
    return false;
}

static void chkunaligned(struct Verifier *v, int64_t target, uint8_t* buf, size_t size) {
    /*
     * We continuously call vchkbundle on the next instruction until we
     * reach an address that is bundle-aligned
     */
    uint8_t* target_insns = buf - (v->addr - v->base) + (target - v->base) ;
    uint32_t bundlesize = v->bundlesize;
    int64_t count = 0;
    int64_t realsize = 0;
    if(target > v->addr) {
        realsize = size - abs(target - (int64_t)v->addr);
    } else {
        realsize = size + abs(target - (int64_t)v->addr);
    }
    FdInstr cur;
    uint64_t old = v->addr;
    v->addr = target;
    while (v->addr % bundlesize != 0 && count < realsize) {
        int len = fd_decode(&target_insns[count], realsize-count, 64, 0, &cur);
        if(len < 0) {
            verrmin(v, "%lx: unknown instruction", v->addr);
        }
        if(!branchto(v, target, &cur)) {
            vchkbundle(v, &target_insns[count], len);
        } else {
            v->addr += len;
        }
        count += len;
    }
    v->addr = old;
}

static void chkbranch(struct Verifier *v, FdInstr *instr, uint8_t* buf, size_t size) {
    int64_t target;
    bool indirect, cond;
    bool branch = branchinfo(v, instr, &target, &indirect, &cond);
    if (branch && !indirect) {
        if (target % v->bundlesize != 0) {
            chkunaligned(v, target, buf, size);
            //verr(v, instr, "jump target is not bundle-aligned");
        }
    } else if (branch && indirect) {
        verr(v, instr, "invalid indirect branch");
    }
}

#include "macroinst.c"

static size_t vchkbundle(struct Verifier *v, uint8_t* buf, size_t size) {
    size_t count = 0;
    size_t ninstr = 0;

    while (count < v->bundlesize && count < size) {
        struct MacroInst mi = macroinst(v, &buf[count], size - count);
        if (mi.size < 0) {
            FdInstr instr;
            int ret = fd_decode(&buf[count], size - count, 64, 0, &instr);
            if (ret < 0) {
                verrmin(v, "%lx: unknown instruction", v->addr);
                return ninstr;
            }
            mi.size = ret;
            mi.ninstr = 1;

            if (!okmnem(v, &instr))
                verr(v, &instr, "illegal instruction");

            chkbranch(v, &instr, &buf[count], size - count);
            chkmem(v, &instr);
            chkmod(v, &instr);
        }

        if (count + mi.size > v->bundlesize) {
            FdInstr instr;
            fd_decode(&buf[count], size - count, 64, 0, &instr);
            printf("Count: %u \n", count);
            printf("Instruction size: %u \n", mi.size);
            verr(v, &instr, "instruction spans bundle boundary");
            v->abort = true; // not useful to give further errors
            return ninstr;
        }

        v->addr += mi.size;
        count += mi.size;
        ninstr += mi.ninstr;
    }
    return ninstr;
}

bool lfiv_verify_x64_large(char *code, size_t size, uintptr_t addr, struct LFIVOptions *opts) {
    uint8_t *insns = (uint8_t *) code;

    struct Verifier v = {
        .addr = addr,
        .opts = opts,
        .bundlesize = 32,
    };

    size_t bdd_count = 0;
    size_t bdd_ninstr = 0;

    uint8_t insn_buf[15] = {0};

    /*
    while (bdd_count < size) {
        uint8_t *insn = &insns[bdd_count];
        if (size - bdd_count < sizeof(insn_buf)) {
            memcpy(insn_buf, &insns[bdd_count], size - bdd_count);
            insn = &insn_buf[0];
        }

        uint8_t n = lfi_x86_bdd(insn);
        if (n == 0) {
            verrmin(&v, "%lx: unknown instruction", v.addr);
            return false;
        }
        v.addr += n;
        bdd_count += n;
        bdd_ninstr++;
    }
    */

    v.addr = addr;
    v.base = addr;

    size_t count = 0;
    size_t ninstr = 0;
    while (count < size) {
        ninstr += vchkbundle(&v, &insns[count], size - count);
        count += v.bundlesize;

        // Exit early if there is no error reporter.
        if ((v.failed && v.opts->err == NULL) || v.abort)
            return false;
    }

    if (!v.failed) {
        //assert(bdd_ninstr == ninstr);
    }

    return !v.failed;
}
