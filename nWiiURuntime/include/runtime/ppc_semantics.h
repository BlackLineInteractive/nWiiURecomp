#pragma once

#include "runtime/cpu_context.h"
#include "runtime/memory.h"

#include <cstdint>

namespace nwii::runtime::ppc {
constexpr bool valid_lswi_form(uint32_t rt, uint32_t ra,
                               uint32_t byte_count) {
    const uint32_t register_count =
        ((byte_count == 0 ? 32 : byte_count) + 3) / 4;
    return !(rt == 0 && ra == 0) &&
           (ra == 0 || ((ra - rt) & 31) >= register_count);
}

// Accounting only: what recompiled blocks need on the hot path.
inline void count_instruction(CPUContext& cpu) { ++cpu.instruction_count; }

inline void trace_instruction(CPUContext& cpu, uint32_t raw_instruction) {
    cpu.current_instruction = raw_instruction;
    cpu.pc_history[cpu.history_cursor] = cpu.pc;
    cpu.history_cursor = (cpu.history_cursor + 1) % cpu.pc_history.size();
    if (cpu.history_size < cpu.pc_history.size()) {
        ++cpu.history_size;
    }
    ++cpu.instruction_count;
}

void mulli(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate);
void subfic(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate);
void cmpli(CPUContext& cpu, uint32_t bf, uint32_t ra, uint32_t immediate);
void cmpi(CPUContext& cpu, uint32_t bf, uint32_t ra, int32_t immediate);
void addic(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate,
           bool record);
void addi(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate);
void addis(CPUContext& cpu, uint32_t rt, uint32_t ra, int32_t immediate);
void rlwinm(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t sh,
            uint32_t mb, uint32_t me, bool rc);
void rlwnm(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb,
           uint32_t mb, uint32_t me, bool rc);
void rlwimi(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t sh,
            uint32_t mb, uint32_t me, bool rc);
void ori(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t immediate);
void xori(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t immediate);
void andi_dot(CPUContext& cpu, uint32_t ra, uint32_t rs,
              uint32_t immediate);

void cmp(CPUContext& cpu, uint32_t bf, uint32_t ra, uint32_t rb);
void cmpl(CPUContext& cpu, uint32_t bf, uint32_t ra, uint32_t rb);
void tw(CPUContext& cpu, uint32_t to, uint32_t ra, uint32_t rb);
void mfcr(CPUContext& cpu, uint32_t rt);
void subf(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void subfc(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void subfe(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void subfze(CPUContext& cpu, uint32_t rt, uint32_t ra, bool oe, bool rc);
void neg(CPUContext& cpu, uint32_t rt, uint32_t ra, bool rc);
void cntlzw(CPUContext& cpu, uint32_t ra, uint32_t rs, bool rc);
void and_(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void andc(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void nor_(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void mulhwu(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void mulhw(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void mullw(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void divwu(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void divw(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool oe,
          bool rc);
void add(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void addc(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool rc);
void addze(CPUContext& cpu, uint32_t rt, uint32_t ra, bool oe, bool rc);
void adde(CPUContext& cpu, uint32_t rt, uint32_t ra, uint32_t rb, bool oe,
          bool rc);
void or_(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void orc(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void xor_(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void srawi(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t sh, bool rc);
void sraw(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void slw(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void srw(CPUContext& cpu, uint32_t ra, uint32_t rs, uint32_t rb, bool rc);
void extsb(CPUContext& cpu, uint32_t ra, uint32_t rs, bool rc);
void extsh(CPUContext& cpu, uint32_t ra, uint32_t rs, bool rc);
void mfspr(CPUContext& cpu, uint32_t rt, uint32_t spr);
void mtspr(CPUContext& cpu, uint32_t rs, uint32_t spr);

void lwz(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement);
void lwzu(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          int32_t displacement);
void lhzu(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          int32_t displacement);
void lhz(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement);
void lha(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement);
void lhau(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          int32_t displacement);
void lbz(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement);
void lbzu(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          int32_t displacement);
void stw(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
         int32_t displacement);
void stwu(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          int32_t displacement);
void stb(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
         int32_t displacement);
void stbu(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          int32_t displacement);
void sth(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
         int32_t displacement);
void sthu(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          int32_t displacement);
void lmw(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
         int32_t displacement);
void stmw(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          int32_t displacement);
void lwzx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t rb);
void lwzux(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb);
void lwarx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb);
void lwbrx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb);
void lbzux(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb);
void lhaux(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb);
void lbzx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t rb);
void lhax(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t rb);
void lhzx(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t rb);
void lhzux(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
           uint32_t rb);
void stwx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          uint32_t rb);
void stwux(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t rb);
void stwcx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t rb);
void stwbrx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
            uint32_t rb);
void stbux(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t rb);
void sthux(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t rb);
void dcbf(CPUContext& cpu);
void dcbst(CPUContext& cpu);
void dcbz(CPUContext& cpu, GuestMemory& memory, uint32_t ra, uint32_t rb);
void stbx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          uint32_t rb);
void sthx(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
          uint32_t rb);
void lswi(CPUContext& cpu, GuestMemory& memory, uint32_t rt, uint32_t ra,
          uint32_t byte_count);
void stswi(CPUContext& cpu, GuestMemory& memory, uint32_t rs, uint32_t ra,
           uint32_t byte_count);
void stfiwx(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
            uint32_t rb);
void stfsx(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
           uint32_t rb);
void lfsx(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
          uint32_t rb);
void lfsux(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
           uint32_t rb);
void stfsux(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
            uint32_t rb);
void psq_l(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
           bool w, uint32_t gqr, int32_t displacement);
void psq_st(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
            bool w, uint32_t gqr, int32_t displacement);
void psq_lu(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
            bool w, uint32_t gqr, int32_t displacement);
void psq_stu(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
             bool w, uint32_t gqr, int32_t displacement);

void lfs(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
         int32_t displacement);
void lfsu(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
          int32_t displacement);
void lfd(CPUContext& cpu, GuestMemory& memory, uint32_t frt, uint32_t ra,
         int32_t displacement);
void stfs(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
          int32_t displacement);
void stfsu(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
           int32_t displacement);
void fdivs(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void fres(CPUContext& cpu, uint32_t frt, uint32_t frb);
void fadds(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void fmuls(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc);
void fmul(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc);
void fmadds(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
            uint32_t frc);
void fmadd(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
           uint32_t frc);
void fnmsub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
           uint32_t frc);
void fmsubs(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
            uint32_t frc);
void fnmsubs(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
             uint32_t frc);
void fmsub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb,
           uint32_t frc);
void stfd(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
          int32_t displacement);
void stfdu(CPUContext& cpu, GuestMemory& memory, uint32_t frs, uint32_t ra,
           int32_t displacement);
void fsubs(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void fsub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void fadd(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void fdiv(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void frsp(CPUContext& cpu, uint32_t frt, uint32_t frb);
void fabs(CPUContext& cpu, uint32_t frt, uint32_t frb);
void fneg(CPUContext& cpu, uint32_t frt, uint32_t frb);
void fnabs(CPUContext& cpu, uint32_t frt, uint32_t frb);
void frsqrte(CPUContext& cpu, uint32_t frt, uint32_t frb);
void fctiw(CPUContext& cpu, uint32_t frt, uint32_t frb);
void fctiwz(CPUContext& cpu, uint32_t frt, uint32_t frb);
void fmr(CPUContext& cpu, uint32_t frt, uint32_t frb);
void fcmpu(CPUContext& cpu, uint32_t bf, uint32_t fra, uint32_t frb);
void fsel(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
          uint32_t frb);
void ps_merge00(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void ps_merge10(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void ps_merge01(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void ps_merge11(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void ps_neg(CPUContext& cpu, uint32_t frt, uint32_t frb);
void ps_muls0(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc);
void ps_mul(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc);
void ps_muls1(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc);
void ps_madds0(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
               uint32_t frb);
void ps_madds1(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
               uint32_t frb);
void ps_madd(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
             uint32_t frb);
void ps_sum0(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
             uint32_t frb);
void ps_sum1(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
             uint32_t frb);
void ps_add(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void ps_sub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frb);
void ps_msub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
             uint32_t frb);
void ps_sel(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
            uint32_t frb);
void ps_cmpo0(CPUContext& cpu, uint32_t bf, uint32_t fra, uint32_t frb);
void ps_rsqrte(CPUContext& cpu, uint32_t frt, uint32_t frb);
void ps_nmsub(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
              uint32_t frb);
void ps_nmadd(CPUContext& cpu, uint32_t frt, uint32_t fra, uint32_t frc,
              uint32_t frb);

uint32_t relocated_branch_target(const GuestMemory& memory, uint32_t address,
                                 uint32_t original, uint32_t fallback);
void b(CPUContext& cpu, uint32_t target, bool link);
void bc(CPUContext& cpu, uint32_t target, uint32_t bo, uint32_t bi,
        bool link);
void bclr(CPUContext& cpu, uint32_t bo, uint32_t bi, bool link);
void bcctr(CPUContext& cpu, uint32_t bo, uint32_t bi, bool link);
void isync(CPUContext& cpu);
void crxor(CPUContext& cpu, uint32_t bt, uint32_t ba, uint32_t bb);
void creqv(CPUContext& cpu, uint32_t bt, uint32_t ba, uint32_t bb);
} // namespace nwii::runtime::ppc

// Definitions are header-inline on purpose. Recompiled blocks call one of
// these per guest instruction; out of line in their own translation unit each
// became a PLT call, which blocked inlining and forced every guest register
// through memory across it. A five instruction block emitted eighteen calls.
#include "runtime/ppc_semantics_inl.h"
