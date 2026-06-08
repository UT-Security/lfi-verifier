#include <assert.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "lfiv.h"
#include "fadec.h"

#ifndef CTXREG_TP_OFFSET
#define CTXREG_TP_OFFSET 16
#endif

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

typedef struct {
    FdInstr instrs[32];
    bool valid[32];
    size_t size;
} FdInstrBundle;

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
static bool reserved(struct Verifier *v, FdInstr *instr, int op_index) {
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

    if (reg == FD_REG_R14 || reg == FD_REG_SP || reg == FD_REG_R15) {
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
                if (FD_OP_DISP(instr, i) != CTXREG_TP_OFFSET ||
                    FD_OP_BASE(instr, i) != FD_REG_NONE ||
                    FD_OP_INDEX(instr, i) != FD_REG_NONE)
                    verr(v, instr, "Can only access +0x10 of segmented memory");
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
                 FD_TYPE(instr) == FDI_TEST ||
                 FD_TYPE(instr) == FDI_PUSH ||
                 FD_TYPE(instr) == FDI_LDMXCSR ||
                 FD_TYPE(instr) == FDI_DIV ||
                 FD_TYPE(instr) == FDI_FLD ||
                 FD_TYPE(instr) == FDI_FLDENV ||
                 FD_TYPE(instr) == FDI_FLDCW ||
                 FD_TYPE(instr) == FDI_FILD ||
                 FD_TYPE(instr) == FDI_IMUL ||
                 FD_TYPE(instr) == FDI_MUL ||
                 FD_TYPE(instr) == FDI_IDIV ||
                 FD_TYPE(instr) == FDI_BT)
                )
                continue;

            if (FD_OP_BASE(instr, i) == FD_REG_R14 &&
                FD_OP_INDEX(instr, i) == FD_REG_NONE &&
                FD_OP_SCALE(instr, i) == 0 &&
                FD_OP_DISP(instr, i) == 0)
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

    if(FD_TYPE(instr) == FDI_PUSH) return;
    for (size_t i = 0; i < 4; i++) {
        if (FD_OP_TYPE(instr, i) == FD_OT_REG && reserved(v, instr, i)) {
            verr(v, instr, "modification of reserved register");
        }
    }
}


static bool branchto(struct Verifier *v, int64_t target, FdInstr* insn) {
    int64_t i_target;
    bool indirect, cond;
    if(branchinfo(v, insn, &i_target, &indirect, &cond)) {
        if(!indirect && (i_target == target))
            return true;
    }
    return false;
}


#include "macroinst.c"

static void vchkins(struct Verifier *v, uint8_t* buf, size_t size, FdInstrBundle *bundle, size_t idx, struct MacroInst* mi);

struct VerifierWork {
    uint8_t* cur;
    uint64_t start;
    uint64_t cur_addr;
    size_t sz;
    size_t remaining;
    struct VerifierWork* next;
};

static bool alreadyChecked(struct Verifier *v, struct VerifierWork* cur, 
    FdInstr* ins, uint64_t* target, bool* b_and_uncond) {
    bool indirect, cond;
    bool isbranch = branchinfo(v, ins, target, &indirect, &cond);
    *b_and_uncond = isbranch && !cond;
    if(isbranch && !indirect) {
        if(*target % v->bundlesize == 0) {
            return true;
        }
        while(cur != 0) {
            if(cur->start == *target) {
                return true;
            }
            cur = cur->next;
        }
        return false;
    }
    *target = 0;
    return false;
}

static bool stop_control_flow(struct Verifier *v, FdInstr *instr) {
    int64_t target;
    bool indirect, cond;
    bool branch = branchinfo(v, instr, &target, &indirect, &cond);
    return (branch && !cond) || (FD_TYPE(instr) == FDI_HLT);
}

struct VerifierWork* make_work(struct Verifier *v, int64_t target, uint8_t* buf, size_t size) {
    struct VerifierWork* vw = malloc(sizeof(*vw));
    vw->start = target;
    vw->cur_addr = target;
    size_t realsize;
    if(target > v->addr) {
        realsize = size - abs(target - (int64_t)v->addr);
    } else {
        realsize = size + abs(target - (int64_t)v->addr);
    }
    //stop whenever this happens first:
    //- we reach a bundle-aligned address
    //- we go back to our starting address
    //- we run out of instructions/end of file
    size_t ins_to_verify = realsize;
    size_t from_bundle = v->bundlesize - (target % v->bundlesize);
    size_t from_start = v->addr > target ? v->addr - target : v->bundlesize;
    if(ins_to_verify > from_bundle) ins_to_verify = from_bundle;
    if(ins_to_verify > from_start) ins_to_verify = from_start;
    vw->sz = ins_to_verify;
    vw->cur = buf - (v->addr - v->base) + (target - v->base);
    vw->remaining = realsize;
    vw->next = 0;
    return vw;
}

struct VerifierWork* process_work(struct Verifier *v, struct VerifierWork* vw) {
    size_t count = 0;
    size_t size = vw->sz;
    uint8_t* buf = vw->cur;
    uint64_t next_target = 0;
    struct MacroInst mi;
    bool b_and_uncond = false;
    FdInstr cur;
    //we need to set v->addr here or branch calculations will be misaligned
    uint64_t old_addr = v->addr;
    v->addr = vw->cur_addr;
    FdInstrBundle bund = {};
    bund.size = 0;
    int i = 0;
    while(count < size) {
        int len = fd_decode(&buf[count], size-count, 64, 0, &bund.instrs[i]);
        if(len < 0) {
            verrmin(v, "%lx: unknown instruction", v->addr);
            exit(-1);
        }
        count += len;
        bund.valid[i] = true;
        bund.size++;
        i++;
        bool cf_break;
        if(cf_break = stop_control_flow(v, &bund.instrs[i-1])) {
            size = count;
            break;
        }
    }
    count = 0;
    i = 0;
    while(count < size) {
        int len = FD_SIZE(&bund.instrs[i]);
        if(alreadyChecked(v, vw, &bund.instrs[i], &next_target, &b_and_uncond)) {
            mi.size = len;
            mi.ninstr = 1;
        } else {
            if(next_target) {
                //add new work to the verifier, and return
                struct VerifierWork* ret = 
                    make_work(v, next_target, &buf[count], vw->remaining);
                vw->cur = buf + count + len;
                if(b_and_uncond) {
                    vw->sz = 0;
                } else {
                    vw->sz -= (count + len);
                }
                vw->remaining -= (count + len);
                vw->cur_addr = v->addr + len;
                v->addr = old_addr;
                return ret;
            } else {
                 vchkins(v, &buf[count], size - count, &bund, i, &mi);
            }
        }
        v->addr += mi.size;
        count += mi.size;
        i += mi.ninstr;
        if(b_and_uncond) break;
    }
    v->addr = old_addr;
    return 0;
}

static void chkunaligned(struct Verifier *v, int64_t target, uint8_t* buf, size_t size) {
    /*
     * We continuously call vchkbundle on the next instruction until we
     * reach an address that is bundle-aligned
     */
    struct VerifierWork* head = make_work(v, target, buf, size);
    while(head != 0) {
        struct VerifierWork* temp = process_work(v, head);
        if(temp) {
            temp->next = head;
            head = temp;
        } else {
            struct VerifierWork* old = head;
            head = head->next;
            free(old);
        }
    }
}


static bool chkbranch(struct Verifier *v, FdInstr *instr, uint8_t* buf, size_t size) {
    int64_t target;
    bool indirect, cond;
    bool branch = branchinfo(v, instr, &target, &indirect, &cond);
    if (branch && !indirect) {
        if (target % v->bundlesize != 0) {
          if (target < v->base || target > (v->addr + size)) {
            verr(v, instr, "Branch target outside of valid space");
          }
          chkunaligned(v, target, buf, size);
        }
    } else if (branch && indirect) {
        verr(v, instr, "invalid indirect branch");
    }
    return branch && !cond;
}

// returns false if we're at an unconditional branch
static void vchkins(struct Verifier *v, uint8_t *buf, size_t size, FdInstrBundle *bundle, size_t idx, struct MacroInst* mi) {
    size_t bundlesize = v->bundlesize;
    *mi = macroinst(v, bundle, idx);
    if (mi->size < 0) {
        FdInstr *instr = &bundle->instrs[idx];
        mi->size = instr->size;
        mi->ninstr = 1;

        if (!okmnem(v, instr)) {
            verr(v, instr, "illegal instruction");
        }

        chkmem(v, instr);
        chkmod(v, instr);
        if(chkbranch(v, instr, buf, size)) {
             //skip over the rest of the instructions
             size_t bundle_off = (bundlesize - (v->addr % bundlesize)); 
             if(bundle_off > size) { 
                 mi->size = size; 
             } else { 
                 mi->size = (bundlesize - (v->addr % bundlesize)); 
             } 
            mi->ninstr = (bundle->size - idx);
        }
    }
}

static size_t vchkbundle(struct Verifier *v, uint8_t* buf, size_t size) {
    size_t bundlesize = v->bundlesize;
    if(v->addr % bundlesize != 0) {
        verrmin(v, "%lx: chkbundle not starting at bundle-aligned boundary", v->addr);
    }
    size_t count = 0;
    size_t ninstr = 0;
    struct MacroInst mi;
    bool cf_break = false;
    uint64_t end = v->addr + bundlesize;

    FdInstrBundle bundle = {};

    size_t i = 0;
    while (count < v->bundlesize && count < size) {
        int ret = fd_decode(&buf[count], size - count, 64, 0, &bundle.instrs[i]);
        if (ret < 0) {
            verrmin(v, "%lx: unknown instruction", v->addr + count);
        }
        count += ret;
        bundle.valid[i] = true;
        bundle.size++;
        i++;
        if(cf_break = stop_control_flow(v, &bundle.instrs[i-1])) {
            break;
        }
    }

    count = 0;
    i = 0;
    while (i < bundle.size) {
        vchkins(v, &buf[count], size - count, &bundle, i, &mi);
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
        i += mi.ninstr;
    }
    if(cf_break) {
        v->addr = end;
        //don't have to inc bundlesize, that's functionally baked in already
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
