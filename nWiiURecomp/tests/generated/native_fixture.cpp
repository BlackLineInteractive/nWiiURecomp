#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x38600005
    ++cpu.instruction_count;
    cpu.gpr[3] = 0 + static_cast<uint32_t>(5);
    cpu.pc += 4;
    // 0x00001004: 0x90610000
    ++cpu.instruction_count;
    memory.write32(cpu.gpr[1] + static_cast<uint32_t>(0), cpu.gpr[3], cpu.pc);
    cpu.pc += 4;
    // 0x00001008: 0x80810000
    ++cpu.instruction_count;
    cpu.gpr[4] = memory.read32(cpu.gpr[1] + static_cast<uint32_t>(0), cpu.pc);
    cpu.pc += 4;
    // 0x0000100C: 0x38840007
    ++cpu.instruction_count;
    cpu.gpr[4] = cpu.gpr[4] + static_cast<uint32_t>(7);
    cpu.pc += 4;
    // 0x00001010: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_surface_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x10221CA0
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_merge10(cpu, 1, 2, 3);
    // 0x00001004: 0x1CC4FFFE
    ++cpu.instruction_count;
    nwii::runtime::ppc::mulli(cpu, 6, 4, -2);
    // 0x00001008: 0x28840001
    ++cpu.instruction_count;
    nwii::runtime::ppc::cmpli(cpu, 1, 4, 1);
    // 0x0000100C: 0x2D04FFFF
    ++cpu.instruction_count;
    nwii::runtime::ppc::cmpi(cpu, 2, 4, -1);
    // 0x00001010: 0x30C40001
    ++cpu.instruction_count;
    nwii::runtime::ppc::addic(cpu, 6, 4, 1, false);
    // 0x00001014: 0x34C40001
    ++cpu.instruction_count;
    nwii::runtime::ppc::addic(cpu, 6, 4, 1, true);
    // 0x00001018: 0x38C40001
    ++cpu.instruction_count;
    cpu.gpr[6] = cpu.gpr[4] + static_cast<uint32_t>(1);
    cpu.pc += 4;
    // 0x0000101C: 0x3CC40001
    ++cpu.instruction_count;
    cpu.gpr[6] = cpu.gpr[4] + (static_cast<uint32_t>(1) << 16);
    cpu.pc += 4;
    // 0x00001020: 0x4C00012C
    ++cpu.instruction_count;
    nwii::runtime::ppc::isync(cpu);
    // 0x00001024: 0x5486422F
    ++cpu.instruction_count;
    nwii::runtime::ppc::rlwinm(cpu, 6, 4, 8, 8, 23, true);
    // 0x00001028: 0x608600F0
    ++cpu.instruction_count;
    cpu.gpr[6] = cpu.gpr[4] | 240;
    cpu.pc += 4;
    // 0x0000102C: 0x6886FFFF
    ++cpu.instruction_count;
    cpu.gpr[6] = cpu.gpr[4] ^ 65535;
    cpu.pc += 4;
    // 0x00001030: 0x7C842800
    ++cpu.instruction_count;
    nwii::runtime::ppc::cmp(cpu, 1, 4, 5);
    // 0x00001034: 0x7C842808
    ++cpu.instruction_count;
    nwii::runtime::ppc::tw(cpu, 4, 4, 5);
    // 0x00001038: 0x7CC1102E
    ++cpu.instruction_count;
    nwii::runtime::ppc::lwzx(cpu, memory, 6, 1, 2);
    // 0x0000103C: 0x7D042840
    ++cpu.instruction_count;
    nwii::runtime::ppc::cmpl(cpu, 2, 4, 5);
    // 0x00001040: 0x7CC42851
    ++cpu.instruction_count;
    nwii::runtime::ppc::subf(cpu, 6, 4, 5, true);
    // 0x00001044: 0x7C8628F9
    ++cpu.instruction_count;
    nwii::runtime::ppc::nor_(cpu, 6, 4, 5, true);
    // 0x00001048: 0x7CC42911
    ++cpu.instruction_count;
    nwii::runtime::ppc::subfe(cpu, 6, 4, 5, true);
    // 0x0000104C: 0x7C81112E
    ++cpu.instruction_count;
    nwii::runtime::ppc::stwx(cpu, memory, 4, 1, 2);
    // 0x00001050: 0x7CC429D7
    ++cpu.instruction_count;
    nwii::runtime::ppc::mullw(cpu, 6, 4, 5, true);
    // 0x00001054: 0x7CC42A15
    ++cpu.instruction_count;
    nwii::runtime::ppc::add(cpu, 6, 4, 5, true);
    // 0x00001058: 0x7CC802A6
    ++cpu.instruction_count;
    nwii::runtime::ppc::mfspr(cpu, 6, 8);
    // 0x0000105C: 0x7C862B79
    ++cpu.instruction_count;
    nwii::runtime::ppc::or_(cpu, 6, 4, 5, true);
    // 0x00001060: 0x7CC903A6
    ++cpu.instruction_count;
    nwii::runtime::ppc::mtspr(cpu, 6, 9);
    // 0x00001064: 0x7C862671
    ++cpu.instruction_count;
    nwii::runtime::ppc::srawi(cpu, 6, 4, 4, true);
    // 0x00001068: 0x7C6117AE
    ++cpu.instruction_count;
    nwii::runtime::ppc::stfiwx(cpu, memory, 3, 1, 2);
    // 0x0000106C: 0x80C10008
    ++cpu.instruction_count;
    cpu.gpr[6] = memory.read32(cpu.gpr[1] + static_cast<uint32_t>(8), cpu.pc);
    cpu.pc += 4;
    // 0x00001070: 0x88C10009
    ++cpu.instruction_count;
    nwii::runtime::ppc::lbz(cpu, memory, 6, 1, 9);
    // 0x00001074: 0x90810010
    ++cpu.instruction_count;
    memory.write32(cpu.gpr[1] + static_cast<uint32_t>(16), cpu.gpr[4], cpu.pc);
    cpu.pc += 4;
    // 0x00001078: 0x98810014
    ++cpu.instruction_count;
    nwii::runtime::ppc::stb(cpu, memory, 4, 1, 20);
    // 0x0000107C: 0xB0810016
    ++cpu.instruction_count;
    nwii::runtime::ppc::sth(cpu, memory, 4, 1, 22);
    // 0x00001080: 0xBBC10020
    ++cpu.instruction_count;
    nwii::runtime::ppc::lmw(cpu, memory, 30, 1, 32);
    // 0x00001084: 0xBFC10028
    ++cpu.instruction_count;
    nwii::runtime::ppc::stmw(cpu, memory, 30, 1, 40);
    // 0x00001088: 0xC0810030
    ++cpu.instruction_count;
    nwii::runtime::ppc::lfs(cpu, memory, 4, 1, 48);
    // 0x0000108C: 0xC8A10038
    ++cpu.instruction_count;
    nwii::runtime::ppc::lfd(cpu, memory, 5, 1, 56);
    // 0x00001090: 0xD0810050
    ++cpu.instruction_count;
    nwii::runtime::ppc::stfs(cpu, memory, 4, 1, 80);
    // 0x00001094: 0xD8A10058
    ++cpu.instruction_count;
    nwii::runtime::ppc::stfd(cpu, memory, 5, 1, 88);
    // 0x00001098: 0xECC21828
    ++cpu.instruction_count;
    nwii::runtime::ppc::fsubs(cpu, 6, 2, 3);
    // 0x0000109C: 0xFD021800
    ++cpu.instruction_count;
    nwii::runtime::ppc::fcmpu(cpu, 2, 2, 3);
    // 0x000010A0: 0xFCE01018
    ++cpu.instruction_count;
    nwii::runtime::ppc::frsp(cpu, 7, 2);
    // 0x000010A4: 0xFD00101E
    ++cpu.instruction_count;
    nwii::runtime::ppc::fctiwz(cpu, 8, 2);
    // 0x000010A8: 0xFD221828
    ++cpu.instruction_count;
    nwii::runtime::ppc::fsub(cpu, 9, 2, 3);
    // 0x000010AC: 0xFD401090
    ++cpu.instruction_count;
    nwii::runtime::ppc::fmr(cpu, 10, 2);
    // 0x000010B0: 0x84C10040
    ++cpu.instruction_count;
    nwii::runtime::ppc::lwzu(cpu, memory, 6, 1, 64);
    // 0x000010B4: 0x8CC10004
    ++cpu.instruction_count;
    nwii::runtime::ppc::lbzu(cpu, memory, 6, 1, 4);
    // 0x000010B8: 0x94810004
    ++cpu.instruction_count;
    nwii::runtime::ppc::stwu(cpu, memory, 4, 1, 4);
    // 0x000010BC: 0x9C810001
    ++cpu.instruction_count;
    nwii::runtime::ppc::stbu(cpu, memory, 4, 1, 1);
    // 0x000010C0: 0x6C8E8000
    ++cpu.instruction_count;
    cpu.gpr[14] = cpu.gpr[4] ^ 2147483648;
    cpu.pc += 4;
    // 0x000010C4: 0x7D6124AA
    ++cpu.instruction_count;
    nwii::runtime::ppc::lswi(cpu, memory, 11, 1, 4);
    // 0x000010C8: 0x7D6125AA
    ++cpu.instruction_count;
    nwii::runtime::ppc::stswi(cpu, memory, 11, 1, 4);
    // 0x000010CC: 0x7D8E0775
    ++cpu.instruction_count;
    nwii::runtime::ppc::extsb(cpu, 14, 12, true);
    // 0x000010D0: 0xB4940002
    ++cpu.instruction_count;
    nwii::runtime::ppc::sthu(cpu, memory, 4, 20, 2);
    // 0x000010D4: 0xC6750004
    ++cpu.instruction_count;
    nwii::runtime::ppc::lfsu(cpu, memory, 19, 21, 4);
    // 0x000010D8: 0xEDC21824
    ++cpu.instruction_count;
    nwii::runtime::ppc::fdivs(cpu, 14, 2, 3);
    // 0x000010DC: 0xEDE2182A
    ++cpu.instruction_count;
    nwii::runtime::ppc::fadds(cpu, 15, 2, 3);
    // 0x000010E0: 0xEE0200F2
    ++cpu.instruction_count;
    nwii::runtime::ppc::fmuls(cpu, 16, 2, 3);
    // 0x000010E4: 0xEE22193A
    ++cpu.instruction_count;
    nwii::runtime::ppc::fmadds(cpu, 17, 2, 3, 4);
    // 0x000010E8: 0xFE401050
    ++cpu.instruction_count;
    nwii::runtime::ppc::fneg(cpu, 18, 2);
    // 0x000010EC: 0x22C40005
    ++cpu.instruction_count;
    nwii::runtime::ppc::subfic(cpu, 22, 4, 5);
    // 0x000010F0: 0x5096821F
    ++cpu.instruction_count;
    nwii::runtime::ppc::rlwimi(cpu, 22, 4, 16, 8, 15, true);
    // 0x000010F4: 0x6496FFFF
    ++cpu.instruction_count;
    cpu.gpr[22] = cpu.gpr[4] | 4294901760;
    cpu.pc += 4;
    // 0x000010F8: 0x7C962831
    ++cpu.instruction_count;
    nwii::runtime::ppc::slw(cpu, 22, 4, 5, true);
    // 0x000010FC: 0x7EE42817
    ++cpu.instruction_count;
    nwii::runtime::ppc::mulhwu(cpu, 23, 4, 5, true);
    // 0x00001100: 0x7F042B97
    ++cpu.instruction_count;
    nwii::runtime::ppc::divwu(cpu, 24, 4, 5, true);
    // 0x00001104: 0x7C992C31
    ++cpu.instruction_count;
    nwii::runtime::ppc::srw(cpu, 25, 4, 5, true);
    // 0x00001108: 0x7C845814
    ++cpu.instruction_count;
    nwii::runtime::ppc::addc(cpu, 4, 4, 11, false);
    // 0x0000110C: 0xFDA06B3A
    ++cpu.instruction_count;
    nwii::runtime::ppc::fmadd(cpu, 13, 0, 13, 12);
    // 0x00001110: 0x7D6BFA78
    ++cpu.instruction_count;
    nwii::runtime::ppc::xor_(cpu, 11, 11, 31, false);
    // 0x00001114: 0xFC200A10
    ++cpu.instruction_count;
    nwii::runtime::ppc::fabs(cpu, 1, 1);
    // 0x00001118: 0xFC1E02F2
    ++cpu.instruction_count;
    nwii::runtime::ppc::fmul(cpu, 0, 30, 11);
    // 0x0000111C: 0xFFC0F01C
    ++cpu.instruction_count;
    nwii::runtime::ppc::fctiw(cpu, 30, 30);
    // 0x00001120: 0x7D7B42AE
    ++cpu.instruction_count;
    nwii::runtime::ppc::lhax(cpu, memory, 11, 27, 8);
    // 0x00001124: 0x7D2B0734
    ++cpu.instruction_count;
    nwii::runtime::ppc::extsh(cpu, 11, 9, false);
    // 0x00001128: 0xDDA904E8
    ++cpu.instruction_count;
    nwii::runtime::ppc::stfdu(cpu, memory, 13, 9, 1256);
    // 0x0000112C: 0x7FA7DB2E
    ++cpu.instruction_count;
    nwii::runtime::ppc::sthx(cpu, memory, 29, 7, 27);
    // 0x00001130: 0xFD496A3C
    ++cpu.instruction_count;
    nwii::runtime::ppc::fnmsub(cpu, 10, 9, 13, 8);
    // 0x00001134: 0x7D6900AE
    ++cpu.instruction_count;
    nwii::runtime::ppc::lbzx(cpu, memory, 11, 9, 0);
    // 0x00001138: 0xFC210024
    ++cpu.instruction_count;
    nwii::runtime::ppc::fdiv(cpu, 1, 1, 0);
    // 0x0000113C: 0xFDAA682A
    ++cpu.instruction_count;
    nwii::runtime::ppc::fadd(cpu, 13, 10, 13);
    // 0x00001140: 0x7C630194
    ++cpu.instruction_count;
    nwii::runtime::ppc::addze(cpu, 3, 3, false, false);
    // 0x00001144: 0x7F5A0595
    ++cpu.instruction_count;
    nwii::runtime::ppc::addze(cpu, 26, 26, true, true);
    // 0x00001148: 0x7CE433D6
    ++cpu.instruction_count;
    nwii::runtime::ppc::divw(cpu, 7, 4, 6, false, false);
    // 0x0000114C: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_subfze_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7CC00191
    ++cpu.instruction_count;
    nwii::runtime::ppc::subfze(cpu, 6, 0, false, true);
    // 0x00001004: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_subfzeo_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C630591
    ++cpu.instruction_count;
    nwii::runtime::ppc::subfze(cpu, 3, 3, true, true);
    // 0x00001004: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_divwo_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7CE437D7
    ++cpu.instruction_count;
    nwii::runtime::ppc::divw(cpu, 7, 4, 6, true, true);
    // 0x00001004: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_bc_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x41820008
    ++cpu.instruction_count;
    nwii::runtime::ppc::bc(cpu, 0x00001008, 12, 2, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_b_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x48000008
    ++cpu.instruction_count;
    nwii::runtime::ppc::b(cpu, 0x00001008, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_bctr_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x4E800420
    ++cpu.instruction_count;
    nwii::runtime::ppc::bcctr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_crxor_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x4CC63182
    ++cpu.instruction_count;
    nwii::runtime::ppc::crxor(cpu, 6, 6, 6);
    // 0x00001004: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_fnmsubs_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xEE4D95FC
    ++cpu.instruction_count;
    nwii::runtime::ppc::fnmsubs(cpu, 18, 13, 18, 23);
    // 0x00001004: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_fmsubs_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xEC095078
    ++cpu.instruction_count;
    nwii::runtime::ppc::fmsubs(cpu, 0, 9, 10, 1);
    // 0x00001004: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_paired_fixture(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x10A601D8
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_muls0(cpu, 5, 6, 7);
    // 0x00001004: 0x110902B2
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_mul(cpu, 8, 9, 10);
    // 0x00001008: 0x116C737A
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_madd(cpu, 11, 12, 13, 14);
    // 0x0000100C: 0x11F09454
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_sum0(cpu, 15, 16, 17, 18);
    // 0x00001010: 0x1007482A
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_add(cpu, 0, 7, 9);
    // 0x00001014: 0x1274A828
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_sub(cpu, 19, 20, 21);
    // 0x00001018: 0x12D7C678
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_msub(cpu, 22, 23, 25, 24);
    // 0x0000101C: 0x135BE4E0
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_merge11(cpu, 26, 27, 28);
    // 0x00001020: 0x13DDFC60
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_merge01(cpu, 30, 29, 31);
    // 0x00001024: 0x10201050
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_neg(cpu, 1, 2);
    // 0x00001028: 0x4E800020
    ++cpu.instruction_count;
    nwii::runtime::ppc::bclr(cpu, 0x14, 0, false);
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_00(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xA3FC0028
    ++cpu.instruction_count;
    nwii::runtime::ppc::lhz(cpu, memory, 31, 28, 40);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_01(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xA7DD0002
    ++cpu.instruction_count;
    nwii::runtime::ppc::lhzu(cpu, memory, 30, 29, 2);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_02(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xA8050000
    ++cpu.instruction_count;
    nwii::runtime::ppc::lha(cpu, memory, 0, 5, 0);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_03(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xD7A80008
    ++cpu.instruction_count;
    nwii::runtime::ppc::stfsu(cpu, memory, 29, 8, 8);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_04(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C000034
    ++cpu.instruction_count;
    nwii::runtime::ppc::cntlzw(cpu, 0, 0, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_05(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D800026
    ++cpu.instruction_count;
    nwii::runtime::ppc::mfcr(cpu, 12);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_06(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7EAB01AE
    ++cpu.instruction_count;
    nwii::runtime::ppc::stbx(cpu, memory, 21, 11, 0);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_07(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C87422E
    ++cpu.instruction_count;
    nwii::runtime::ppc::lhzx(cpu, memory, 4, 7, 8);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_08(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D800038
    ++cpu.instruction_count;
    nwii::runtime::ppc::and_(cpu, 0, 12, 0, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_09(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C09526E
    ++cpu.instruction_count;
    nwii::runtime::ppc::lhzux(cpu, memory, 0, 9, 10);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_10(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7CECD42E
    ++cpu.instruction_count;
    nwii::runtime::ppc::lfsx(cpu, memory, 7, 12, 26);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_11(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7DAC5C6E
    ++cpu.instruction_count;
    nwii::runtime::ppc::lfsux(cpu, memory, 13, 12, 11);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_12(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D296078
    ++cpu.instruction_count;
    nwii::runtime::ppc::andc(cpu, 9, 9, 12, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_13(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C034D2E
    ++cpu.instruction_count;
    nwii::runtime::ppc::stfsx(cpu, memory, 0, 3, 9);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_14(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D4700D0
    ++cpu.instruction_count;
    nwii::runtime::ppc::neg(cpu, 10, 7, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_15(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D404096
    ++cpu.instruction_count;
    nwii::runtime::ppc::mulhw(cpu, 10, 0, 8, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_16(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xFC2D006E
    ++cpu.instruction_count;
    nwii::runtime::ppc::fsel(cpu, 1, 13, 1, 0);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_17(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xFC205034
    ++cpu.instruction_count;
    nwii::runtime::ppc::frsqrte(cpu, 1, 10);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_18(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x4CC63242
    ++cpu.instruction_count;
    nwii::runtime::ppc::creqv(cpu, 6, 6, 6);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_19(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x5F84203E
    ++cpu.instruction_count;
    nwii::runtime::ppc::rlwnm(cpu, 4, 28, 4, 0, 31, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_20(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x70E000FD
    ++cpu.instruction_count;
    nwii::runtime::ppc::andi_dot(cpu, 0, 7, 253);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_21(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x75491028
    ++cpu.instruction_count;
    nwii::runtime::ppc::andi_dot(cpu, 9, 10, 271056896);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_22(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xACE8FDE8
    ++cpu.instruction_count;
    nwii::runtime::ppc::lhau(cpu, memory, 7, 8, -536);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_23(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C003FEC
    ++cpu.instruction_count;
    nwii::runtime::ppc::dcbz(cpu, memory, 0, 7);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_24(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D29256E
    ++cpu.instruction_count;
    nwii::runtime::ppc::stfsux(cpu, memory, 9, 9, 4);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_25(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D2A0630
    ++cpu.instruction_count;
    nwii::runtime::ppc::sraw(cpu, 10, 9, 0, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_26(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D6CD86E
    ++cpu.instruction_count;
    nwii::runtime::ppc::lwzux(cpu, memory, 11, 12, 27);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_27(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7CBB516E
    ++cpu.instruction_count;
    nwii::runtime::ppc::stwux(cpu, memory, 5, 27, 10);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_28(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7FE82010
    ++cpu.instruction_count;
    nwii::runtime::ppc::subfc(cpu, 31, 8, 4, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_29(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xE1810024
    ++cpu.instruction_count;
    nwii::runtime::ppc::psq_l(cpu, memory, 12, 1, false, 0, 36);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_30(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xF381005C
    ++cpu.instruction_count;
    nwii::runtime::ppc::psq_st(cpu, memory, 28, 1, false, 0, 92);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_31(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xFC006910
    ++cpu.instruction_count;
    nwii::runtime::ppc::fnabs(cpu, 0, 13);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_32(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xFD6D6EF8
    ++cpu.instruction_count;
    nwii::runtime::ppc::fmsub(cpu, 11, 13, 13, 27);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_33(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x110D3420
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_merge00(cpu, 8, 13, 6);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_34(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x11AB68DC
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_madds0(cpu, 13, 11, 3, 13);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_35(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x118249DE
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_madds1(cpu, 12, 2, 7, 9);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_36(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x100001D6
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_sum1(cpu, 0, 0, 7, 0);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_37(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x118500DA
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_muls1(cpu, 12, 5, 3);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_38(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x1085312E
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_sel(cpu, 4, 5, 4, 6);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_39(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x10073040
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_cmpo0(cpu, 0, 7, 6);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_40(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xECA06030
    ++cpu.instruction_count;
    nwii::runtime::ppc::fres(cpu, 5, 12);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_41(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7FE7336E
    ++cpu.instruction_count;
    nwii::runtime::ppc::sthux(cpu, memory, 31, 7, 6);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_42(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D006028
    ++cpu.instruction_count;
    nwii::runtime::ppc::lwarx(cpu, memory, 8, 0, 12);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_43(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C00F06C
    ++cpu.instruction_count;
    nwii::runtime::ppc::dcbst(cpu);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_44(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D8A4114
    ++cpu.instruction_count;
    nwii::runtime::ppc::adde(cpu, 12, 10, 8, false, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_45(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7CE0552C
    ++cpu.instruction_count;
    nwii::runtime::ppc::stwbrx(cpu, memory, 7, 0, 10);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_46(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7ED461EE
    ++cpu.instruction_count;
    nwii::runtime::ppc::stbux(cpu, memory, 22, 20, 12);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_47(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D0618EE
    ++cpu.instruction_count;
    nwii::runtime::ppc::lbzux(cpu, memory, 8, 6, 3);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_48(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C00642C
    ++cpu.instruction_count;
    nwii::runtime::ppc::lwbrx(cpu, memory, 0, 0, 12);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_49(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C0C5AEE
    ++cpu.instruction_count;
    nwii::runtime::ppc::lhaux(cpu, memory, 0, 12, 11);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_50(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C1E60AC
    ++cpu.instruction_count;
    nwii::runtime::ppc::dcbf(cpu);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_51(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D62E3A6
    ++cpu.instruction_count;
    nwii::runtime::ppc::mtspr(cpu, 11, 898);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_52(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xE0FDB000
    ++cpu.instruction_count;
    nwii::runtime::ppc::psq_l(cpu, memory, 7, 29, true, 3, 0);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_53(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xF166B000
    ++cpu.instruction_count;
    nwii::runtime::ppc::psq_st(cpu, memory, 11, 6, true, 3, 0);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_54(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xE4C40004
    ++cpu.instruction_count;
    nwii::runtime::ppc::psq_lu(cpu, memory, 6, 4, false, 0, 4);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_55(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x10C04034
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_rsqrte(cpu, 6, 8);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_56(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x10EC583E
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_nmadd(cpu, 7, 12, 0, 11);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_57(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x112561FC
    ++cpu.instruction_count;
    nwii::runtime::ppc::ps_nmsub(cpu, 9, 5, 7, 12);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_58(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D4A3338
    ++cpu.instruction_count;
    nwii::runtime::ppc::orc(cpu, 10, 10, 6, false);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_59(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7D83E3A6
    ++cpu.instruction_count;
    nwii::runtime::ppc::mtspr(cpu, 12, 899);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_60(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0xF5850004
    ++cpu.instruction_count;
    nwii::runtime::ppc::psq_stu(cpu, memory, 12, 5, false, 0, 4);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_61(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C04E3A6
    ++cpu.instruction_count;
    nwii::runtime::ppc::mtspr(cpu, 0, 900);
    cpu.pc = 0x00001004;
}
#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"

extern "C" void native_wwhd_62(nwii::runtime::CPUContext& cpu,
                               nwii::runtime::GuestMemory& memory) {
    (void)memory;
    // 0x00001000: 0x7C05E3A6
    ++cpu.instruction_count;
    nwii::runtime::ppc::mtspr(cpu, 0, 901);
    cpu.pc = 0x00001004;
}
