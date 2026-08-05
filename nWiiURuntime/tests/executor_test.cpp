#include "nwiiu/analyzer/ppc_instruction.h"
#include "runtime/executor.h"
#include "runtime/ppc_semantics.h"
#include "test_support.h"

#include <array>
#include <bit>
#include <cstdint>
#include <initializer_list>
#include <span>
#include <vector>
#include <limits>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::ExecutionImage;
using nwii::runtime::ExecutionStop;
using nwii::runtime::HleAction;
using nwii::runtime::SliceCategory;
using nwii::runtime::Executor;
using nwii::runtime::StopCategory;

constexpr uint32_t kCode = 0x02000000;
constexpr uint32_t kData = 0x10000000;
constexpr uint32_t kImport = 0xC0009BC8;

constexpr uint32_t d_form(uint32_t opcode, uint32_t rt, uint32_t ra,
                          uint32_t immediate) {
    return (opcode << 26) | (rt << 21) | (ra << 16) |
           (immediate & 0xFFFF);
}

constexpr uint32_t x_form(uint32_t opcode, uint32_t rt, uint32_t ra,
                          uint32_t rb, uint32_t xo, bool rc = false) {
    return (opcode << 26) | (rt << 21) | (ra << 16) | (rb << 11) |
           (xo << 1) | static_cast<uint32_t>(rc);
}

constexpr uint32_t a_form(uint32_t opcode, uint32_t frt, uint32_t fra,
                          uint32_t frb, uint32_t frc, uint32_t xo) {
    return (opcode << 26) | (frt << 21) | (fra << 16) | (frb << 11) |
           (frc << 6) | (xo << 1);
}

constexpr uint32_t xl_form(uint32_t bo, uint32_t bi, uint32_t xo,
                           bool link = false) {
    return (19U << 26) | (bo << 21) | (bi << 16) | (xo << 1) |
           static_cast<uint32_t>(link);
}

constexpr uint32_t rlwinm(uint32_t rs, uint32_t ra, uint32_t sh, uint32_t mb,
                          uint32_t me, bool rc = false) {
    return (21U << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) |
           (me << 1) | static_cast<uint32_t>(rc);
}

constexpr uint32_t rlwimi(uint32_t rs, uint32_t ra, uint32_t sh, uint32_t mb,
                          uint32_t me, bool rc = false) {
    return (20U << 26) | (rs << 21) | (ra << 16) | (sh << 11) | (mb << 6) |
           (me << 1) | static_cast<uint32_t>(rc);
}

constexpr uint32_t spr_form(uint32_t rs_rt, uint32_t spr, uint32_t xo) {
    return (31U << 26) | (rs_rt << 21) | ((spr & 0x1F) << 16) |
           ((spr >> 5) << 11) | (xo << 1);
}

uint64_t double_bits(double value) { return std::bit_cast<uint64_t>(value); }

double lane0(const CPUContext& cpu, uint32_t reg) {
    return std::bit_cast<double>(cpu.fpr[reg][0]);
}

ExecutionImage make_image(std::initializer_list<uint32_t> words,
                          bool executable = true) {
    ExecutionImage image;
    std::vector<uint8_t> bytes;
    bytes.reserve(words.size() * sizeof(uint32_t));
    for (const uint32_t word : words) {
        bytes.push_back(static_cast<uint8_t>(word >> 24));
        bytes.push_back(static_cast<uint8_t>(word >> 16));
        bytes.push_back(static_cast<uint8_t>(word >> 8));
        bytes.push_back(static_cast<uint8_t>(word));
    }
    image.memory.map(kCode, static_cast<uint32_t>(bytes.size()),
                     {true, false, executable}, bytes);
    image.memory.map(kData, 0x100, {true, true, false});
    return image;
}

void step_word(uint32_t word, CPUContext& cpu, ExecutionImage& image) {
    image.memory.patch32(kCode, word);
    cpu.pc = kCode;
    Executor(image).step(cpu);
}

HleAction exit_hle(CPUContext& cpu, nwii::runtime::GuestMemory&) {
    cpu.gpr[3] = 0x55;
    return HleAction::exit;
}

HleAction returning_hle(CPUContext& cpu, nwii::runtime::GuestMemory&) {
    cpu.gpr[3] = 0x12345678;
    return HleAction::return_to_lr;
}

HleAction rescheduling_hle(CPUContext&, nwii::runtime::GuestMemory&) {
    return HleAction::reschedule;
}

void native_exit(CPUContext& cpu, nwii::runtime::GuestMemory&) {
    nwii::runtime::ppc::trace_instruction(cpu, 0x60000000);
    cpu.gpr[4] = 0xDEADBEEF;
    cpu.running = false;
}
uint32_t patch_hits;

void patched_exit(CPUContext& cpu, nwii::runtime::GuestMemory&) {
    ++patch_hits;
    nwii::runtime::ppc::trace_instruction(cpu, 0x60000000);
    cpu.gpr[4] = 0xBEEF;
    cpu.running = false;
}

void chained_native(CPUContext& cpu, nwii::runtime::GuestMemory&) {
    while (cpu.instruction_count < cpu.native_instruction_endpoint) {
        if (cpu.native_executor != nullptr &&
            cpu.native_executor->is_patched(cpu.pc)) {
            return;
        }
        switch (cpu.pc) {
        case kCode:
            nwii::runtime::ppc::trace_instruction(cpu, 0x48000004);
            cpu.pc = kCode + 4;
            break;
        case kCode + 4:
            cpu.gpr[5] = 0xDEADBEEF;
            cpu.running = false;
            return;
        default:
            return;
        }
    }
}

void require_guest_fault(const ExecutionStop& stop, const char* message) {
    test::require(stop.category == StopCategory::guest_fault, message);
}

void test_decoder_fields() {
    constexpr nwiiu::analyzer::PpcInstruction addi{0x3860FFFB};
    static_assert(addi.raw() == 0x3860FFFB);
    static_assert(addi.rt() == 3 && addi.rs() == 3 && addi.ra() == 0);
    static_assert(addi.simm() == -5);
    constexpr nwiiu::analyzer::PpcInstruction mr{0x7C9F2378};
    static_assert(mr.rs() == 4 && mr.ra() == 31 && mr.rb() == 4);
    constexpr nwiiu::analyzer::PpcInstruction beq{0x41820008};
    static_assert(beq.bo() == 12 && beq.bi() == 2);
    constexpr nwiiu::analyzer::PpcInstruction mflr{0x7C0802A6};
    static_assert(mflr.spr() == 8);
    constexpr nwiiu::analyzer::PpcInstruction or_record{0x7C9F2379};
    static_assert(or_record.rc());
    constexpr nwiiu::analyzer::PpcInstruction lhzu{0xA4EA0002};
    static_assert(lhzu.opcode() == 41 && lhzu.rt() == 7 && lhzu.ra() == 10 &&
                  lhzu.simm() == 2);
    constexpr nwiiu::analyzer::PpcInstruction stfsx{0x7FDD052E};
    static_assert(stfsx.opcode() == 31 && stfsx.extended_opcode() == 663 &&
                  stfsx.rs() == 30 && stfsx.ra() == 29 && stfsx.rb() == 0);
    constexpr nwiiu::analyzer::PpcInstruction psq_l{0xE0E70000};
    static_assert(psq_l.opcode() == 56 && psq_l.rt() == 7 &&
                  psq_l.ra() == 7 && !psq_l.ps_w() && psq_l.ps_i() == 0 &&
                  psq_l.ps_displacement() == 0);
    constexpr nwiiu::analyzer::PpcInstruction ps_muls0{0x11A70598};
    static_assert(ps_muls0.opcode() == 4 && ps_muls0.xo5() == 12 &&
                  ps_muls0.rt() == 13 && ps_muls0.ra() == 7 &&
                  ps_muls0.frc() == 22 && ps_muls0.rb() == 0);
    constexpr nwiiu::analyzer::PpcInstruction psq_st{0xF1A10010};
    static_assert(psq_st.opcode() == 60 && psq_st.rt() == 13 &&
                  psq_st.ra() == 1 && !psq_st.ps_w() && psq_st.ps_i() == 0 &&
                  psq_st.ps_displacement() == 16);
    constexpr nwiiu::analyzer::PpcInstruction fsel{0xFF6CD32E};
    static_assert(fsel.opcode() == 63 && fsel.xo5() == 23 &&
                  fsel.rt() == 27 && fsel.ra() == 12 && fsel.rb() == 26 &&
                  fsel.frc() == 12);
    constexpr nwiiu::analyzer::PpcInstruction ps_mul{0x104200B2};
    static_assert(ps_mul.opcode() == 4 && ps_mul.xo5() == 25 &&
                  ps_mul.rt() == 2 && ps_mul.ra() == 2 && ps_mul.frc() == 2 &&
                  ps_mul.rb() == 0);
    constexpr nwiiu::analyzer::PpcInstruction ps_madd{0x102310FA};
    static_assert(ps_madd.opcode() == 4 && ps_madd.xo5() == 29 &&
                  ps_madd.rt() == 1 && ps_madd.ra() == 3 &&
                  ps_madd.rb() == 2 && ps_madd.frc() == 3);
    constexpr nwiiu::analyzer::PpcInstruction ps_sum0{0x10211094};
    static_assert(ps_sum0.opcode() == 4 && ps_sum0.xo5() == 10 &&
                  ps_sum0.rt() == 1 && ps_sum0.ra() == 1 &&
                  ps_sum0.rb() == 2 && ps_sum0.frc() == 2);
    constexpr nwiiu::analyzer::PpcInstruction frsqrte{0xFC800834};
    static_assert(frsqrte.opcode() == 63 && frsqrte.extended_opcode() == 26 &&
                  frsqrte.rt() == 4 && frsqrte.ra() == 0 && frsqrte.rb() == 1);
    constexpr nwiiu::analyzer::PpcInstruction fmsubs{0xEC095078};
    static_assert(fmsubs.opcode() == 59 && fmsubs.xo5() == 28 &&
                  fmsubs.rt() == 0 && fmsubs.ra() == 9 && fmsubs.rb() == 10 &&
                  fmsubs.frc() == 1);
}

void test_integer_memory_and_spr_instructions() {
    auto image = make_image({0});
    CPUContext cpu;
    cpu.gpr[1] = kData + 0x40;
    cpu.lr = 0xAABBCCDD;

    step_word(0x38600005, cpu, image); // addi r3,r0,5
    test::require(cpu.gpr[3] == 5 && cpu.pc == kCode + 4, "addi semantics");
    step_word(0x90610000, cpu, image); // stw r3,0(r1)
    test::require(image.memory.read32(kData + 0x40, 0) == 5,
                  "stw big-endian store");
    step_word(0x80810000, cpu, image); // lwz r4,0(r1)
    test::require(cpu.gpr[4] == 5, "lwz big-endian load");

    const uint32_t old_stack = cpu.gpr[1];
    step_word(0x9421FFE8, cpu, image); // stwu r1,-24(r1)
    test::require(cpu.gpr[1] == old_stack - 24 &&
                      image.memory.read32(old_stack - 24, 0) == old_stack,
                  "stwu stores old base before update");

    cpu.gpr[4] = 0xA5A5000F;
    step_word(0x7C9F2378, cpu, image); // or r31,r4,r4
    test::require(cpu.gpr[31] == cpu.gpr[4], "or register semantics");
    step_word(0x7C0802A6, cpu, image); // mflr r0
    test::require(cpu.gpr[0] == 0xAABBCCDD, "mflr semantics");
    cpu.gpr[0] = 0x11223344;
    step_word(0x7C0803A6, cpu, image); // mtlr r0
    test::require(cpu.lr == 0x11223344, "mtlr semantics");

    cpu.ctr = 0x55667788;
    step_word(0x7CA902A6, cpu, image); // mfctr r5
    test::require(cpu.gpr[5] == 0x55667788, "mfctr semantics");
    cpu.gpr[5] = 0x99AABBCC;
    step_word(0x7CA903A6, cpu, image); // mtctr r5
    test::require(cpu.ctr == 0x99AABBCC, "mtctr semantics");
    test::require(cpu.instruction_count == 9 && cpu.history_size == 9,
                  "successful steps traced exactly once");
}

void test_reserved_stwu() {
    constexpr uint32_t kReservedStwu = 0x94000000; // stwu r0,0(r0)
    auto image = make_image({kReservedStwu});
    image.memory.map(0, 4, {true, true, false});
    image.memory.write32(0, 0x11223344, 0);
    CPUContext cpu;
    cpu.pc = kCode;
    cpu.gpr[0] = 0xAABBCCDD;

    const auto stop = Executor(image).run(cpu, 10);

    require_guest_fault(stop, "reserved stwu category");
    test::require(stop.raw_instruction.has_value() &&
                      *stop.raw_instruction == kReservedStwu &&
                      stop.instruction_count == 1 && stop.history_size == 1 &&
                      stop.history[0] == kCode,
                  "reserved stwu is traced exactly once");
    test::require(cpu.gpr[0] == 0xAABBCCDD &&
                      image.memory.read32(0, 0) == 0x11223344,
                  "reserved stwu leaves r0 and memory unchanged");
}

void test_opcode31_low_bit() {
    auto image = make_image({0});
    CPUContext cpu;

    cpu.gpr[4] = 0x80000000;
    step_word(0x7C9F2379, cpu, image); // or. r31,r4,r4
    test::require(cpu.cr[0] == 0x8, "or. sets CR0 LT");
    cpu.gpr[4] = 1;
    step_word(0x7C9F2379, cpu, image);
    test::require(cpu.cr[0] == 0x4, "or. sets CR0 GT");
    cpu.gpr[4] = 0;
    step_word(0x7C9F2379, cpu, image);
    test::require(cpu.cr[0] == 0x2, "or. sets CR0 EQ");
    cpu.xer = 0x80000000;
    step_word(0x7C9F2379, cpu, image);
    test::require(cpu.cr[0] == 0x3, "or. copies XER SO into CR0");

    {
        auto invalid_image = make_image({0x7C0802A7}); // mflr with reserved bit
        CPUContext invalid_cpu;
        invalid_cpu.pc = kCode;
        invalid_cpu.gpr[0] = 0xA5A5A5A5;
        const auto stop = Executor(invalid_image).run(invalid_cpu, 10);
        require_guest_fault(stop, "low-bit mfspr category");
        test::require(stop.pc == kCode &&
                          stop.instruction_count == 1 &&
                          stop.history_size == 1 &&
                          stop.history[0] == kCode &&
                          invalid_cpu.gpr[0] == 0xA5A5A5A5,
                      "low-bit mfspr is traced then rejected without mutation");
    }
    {
        auto invalid_image = make_image({0x7C0803A7}); // mtlr with reserved bit
        CPUContext invalid_cpu;
        invalid_cpu.pc = kCode;
        invalid_cpu.lr = 0xA5A5A5A5;
        invalid_cpu.gpr[0] = 0x11223344;
        const auto stop = Executor(invalid_image).run(invalid_cpu, 10);
        require_guest_fault(stop, "low-bit mtspr category");
        test::require(stop.pc == kCode &&
                          stop.instruction_count == 1 &&
                          stop.history_size == 1 &&
                          stop.history[0] == kCode &&
                          invalid_cpu.lr == 0xA5A5A5A5,
                      "low-bit mtspr is traced then rejected without mutation");
    }
}

void test_unaligned_word_faults() {
    auto image = make_image({0});
    CPUContext cpu;
    cpu.gpr[1] = kData + 0x40;
    image.memory.write32(kData + 0x40, 0x01020304, 0);

    step_word(0x80810001, cpu, image); // lwz r4,1(r1)
    test::require(cpu.gpr[4] == 0x02030400,
                  "lwz supports the title's packed unaligned fields");

    cpu.gpr[3] = 0;
    step_word(0x84610001, cpu, image); // lwzu r3,1(r1)
    test::require(cpu.gpr[3] == 0x02030400 &&
                      cpu.gpr[1] == kData + 0x41,
                  "lwzu supports the title's packed unaligned elements");
    cpu.gpr[1] = kData + 0x40;
    cpu.gpr[5] = 0;
    cpu.gpr[2] = 1;
    step_word(x_form(31, 5, 1, 2, 23), cpu, image); // lwzx r5,r1,r2
    test::require(cpu.gpr[5] == 0x02030400,
                  "lwzx supports the title's packed unaligned fields");

    cpu.gpr[3] = 0xDEADBEEF;
    step_word(0x90610001, cpu, image); // stw r3,1(r1)
    test::require(image.memory.read32(kData + 0x41, 0) == 0xDEADBEEF,
                  "stw supports the title's packed unaligned fields");

    cpu.gpr[1] = kData + 0x40;
    cpu.fpr[1][0] = 0x1122334455667788;
    step_word(0xD8210001, cpu, image); // stfd f1,1(r1)
    step_word(0xC8410001, cpu, image); // lfd f2,1(r1)
    test::require(
        image.memory.read64(kData + 0x41, 0) == 0x1122334455667788 &&
            cpu.fpr[2][0] == 0x1122334455667788,
        "double loads and stores support packed unaligned fields");

    const uint32_t old_stack = cpu.gpr[1];
    step_word(0x9421FFE9, cpu, image); // stwu r1,-23(r1)
    test::require(cpu.gpr[1] == old_stack - 23 &&
                      image.memory.read32(old_stack - 23, 0) == old_stack,
                  "stwu stores before updating an unaligned base");
}

void test_direct_and_conditional_branches() {
    auto image = make_image({0});
    CPUContext cpu;

    step_word(0x48000009, cpu, image); // bl +8
    test::require(cpu.pc == kCode + 8 && cpu.lr == kCode + 4,
                  "linked direct branch");

    cpu.cr[0] = 0x2;
    step_word(0x41820008, cpu, image); // beq +8
    test::require(cpu.pc == kCode + 8, "beq takes when CR0 EQ is set");
    cpu.cr[0] = 0;
    step_word(0x41820008, cpu, image);
    test::require(cpu.pc == kCode + 4, "beq falls through when CR0 EQ is clear");

    cpu.ctr = 2;
    step_word(0x42000008, cpu, image); // bdnz +8
    test::require(cpu.ctr == 1 && cpu.pc == kCode + 8,
                  "bdnz decrements and takes while nonzero");
    cpu.ctr = 1;
    step_word(0x42000008, cpu, image);
    test::require(cpu.ctr == 0 && cpu.pc == kCode + 4,
                  "bdnz falls through at zero");
}

void test_crxor_condition_register_logic() {
    auto image = make_image({0});
    CPUContext cpu;

    // Reached idiom crxor 6,6,6 (0x4CC63182): BA == BB clears CR bit 6. CR bit
    // 6 lives in field 1, nibble bit (3 - 6 % 4) == 1 (value 0x2).
    cpu.cr[1] = 0xF;
    step_word(0x4CC63182, cpu, image);
    test::require(cpu.cr[1] == 0xD && cpu.pc == kCode + 4,
                  "crxor 6,6,6 clears CR bit 6 and advances PC");

    // General XOR: crxor 2,0,1 (0x4C400982) sets CR bit 2 = bit0 ^ bit1. With
    // CR0 = 0x8 (bit0 set, bit1 clear) the result bit is 1, so CR0 -> 0xA.
    cpu.cr[0] = 0x8;
    step_word(0x4C400982, cpu, image);
    test::require(cpu.cr[0] == 0xA && cpu.pc == kCode + 4,
                  "crxor 2,0,1 XORs distinct CR bits into the target");

    // crxor 2,0,1 again with both source bits set (CR0 bit0 and bit1) yields 0.
    cpu.cr[0] = 0xC;
    step_word(0x4C400982, cpu, image);
    test::require(cpu.cr[0] == 0xC,
                  "crxor of two set CR bits clears the target bit");

    // The reserved link bit must stay zero; crxor with LK set is a hard fault.
    auto reserved = make_image({0x4CC63183});
    CPUContext faulted;
    faulted.pc = kCode;
    require_guest_fault(Executor(reserved).run(faulted, 1),
                        "crxor with reserved LK bit is a hard fault");
}

void test_indirect_branches() {
    auto image = make_image({0});
    CPUContext cpu;

    cpu.lr = kCode + 0x40;
    step_word(0x4E800020, cpu, image); // blr
    test::require(cpu.pc == kCode + 0x40, "blr branches to LR");

    cpu.ctr = kCode + 0x44;
    cpu.lr = 0;
    step_word(0x4E800420, cpu, image); // bctr
    test::require(cpu.pc == kCode + 0x44 && cpu.lr == 0,
                  "bctr branches without link");
    cpu.ctr = kCode + 0x48;
    step_word(0x4E800421, cpu, image); // bctrl
    test::require(cpu.pc == kCode + 0x48 && cpu.lr == kCode + 4,
                  "bctrl branches and links");
}

void test_branch_override() {
    auto image = make_image({0x48000009});
    image.branch_overrides.emplace(kCode, kImport);
    CPUContext cpu;
    cpu.pc = kCode;
    Executor(image).step(cpu);
    test::require(cpu.pc == kImport && cpu.lr == kCode + 4,
                  "branch override precedes encoded target and preserves link");

    image.memory.patch32(kCode, 0x48000101);
    test::require(
        nwii::runtime::ppc::relocated_branch_target(
            image.memory, kCode, 0x48000009, kImport) == kCode + 0x100,
        "relocated branch follows the host instruction");
    image.memory.patch32(kCode, 0x4BFFFF01);
    test::require(
        nwii::runtime::ppc::relocated_branch_target(
            image.memory, kCode, 0x48000009, kImport) == kCode - 0x100,
        "relocated branch sign-extends a backward displacement");
    image.memory.patch32(kCode, 0x48000009);
    test::require(
        nwii::runtime::ppc::relocated_branch_target(
            image.memory, kCode, 0x48000009, kImport) == kImport,
        "unchanged branch uses the static relocation target");
}

void test_stops_and_failed_instruction_accounting() {
    {
        auto image = make_image({0x00000000});
        CPUContext cpu;
        cpu.pc = kCode;
        const auto stop = Executor(image).run(cpu, 10);
        require_guest_fault(stop, "unsupported opcode category");
        test::require(stop.pc == kCode && stop.raw_instruction.has_value() &&
                          *stop.raw_instruction == 0 &&
                          stop.fault_address == kCode &&
                          stop.fault_width == 4 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::execute &&
                          stop.instruction_count == 1 &&
                          stop.history_size == 1 &&
                          stop.history[0] == kCode,
                      "unsupported opcode retains execute-fault context");
    }
    {
        auto image = make_image({0x4C000000}); // unsupported opcode-19 XO
        CPUContext cpu;
        cpu.pc = kCode;
        const auto stop = Executor(image).run(cpu, 10);
        require_guest_fault(stop, "unsupported XO category");
        test::require(stop.raw_instruction.has_value() &&
                          *stop.raw_instruction == 0x4C000000 &&
                          stop.instruction_count == 1 &&
                          stop.history_size == 1 &&
                          stop.history[0] == kCode,
                      "unsupported XO is traced once");
    }
    {
        auto image = make_image({0x7CCA02A6}); // mfspr r6,SPR10
        CPUContext cpu;
        cpu.pc = kCode;
        const auto stop = Executor(image).run(cpu, 10);
        require_guest_fault(stop, "unsupported SPR category");
        test::require(stop.raw_instruction.has_value() &&
                          *stop.raw_instruction == 0x7CCA02A6 &&
                          stop.instruction_count == 1 &&
                          stop.history_size == 1 &&
                          stop.history[0] == kCode,
                      "unsupported SPR is traced once");
    }
    {
        auto image = make_image({0x4C000420}); // bcctr with CTR decrement
        CPUContext cpu;
        cpu.pc = kCode;
        cpu.ctr = 7;
        const auto stop = Executor(image).run(cpu, 10);
        require_guest_fault(stop, "invalid bcctr category");
        test::require(cpu.ctr == 7 && stop.raw_instruction.has_value() &&
                          *stop.raw_instruction == 0x4C000420 &&
                          stop.instruction_count == 1 &&
                          stop.history_size == 1 &&
                          stop.history[0] == kCode,
                      "invalid bcctr is traced once before rejection");
    }
    {
        auto image = make_image({0x38600005}, false);
        CPUContext cpu;
        cpu.pc = kCode;
        const auto stop = Executor(image).run(cpu, 10);
        require_guest_fault(stop, "non-executable fetch category");
        test::require(stop.fault_address == kCode &&
                          stop.fault_width == 4 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::execute &&
                          !stop.raw_instruction.has_value() &&
                          stop.instruction_count == 0 && stop.history_size == 0,
                      "failed fetch has no instruction context and is uncounted");
    }
    {
        auto image = make_image({0x80810000}); // lwz r4,0(r1)
        CPUContext cpu;
        cpu.pc = kCode;
        cpu.gpr[1] = 0x20000000;
        const auto stop = Executor(image).run(cpu, 10);
        require_guest_fault(stop, "data access category");
        test::require(stop.fault_address == 0x20000000 &&
                          stop.fault_width == 4 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::read &&
                          stop.raw_instruction.has_value() &&
                          *stop.raw_instruction == 0x80810000 &&
                          stop.instruction_count == 1 &&
                          stop.history_size == 1 &&
                          stop.history[0] == kCode,
                      "data fault retains access and fetched instruction");
    }
    {
        auto image = make_image({0x48000001});
        image.branch_overrides.emplace(kCode, kCode + 2);
        CPUContext cpu;
        cpu.pc = kCode;
        cpu.lr = 0xCAFEBABE;
        const auto stop = Executor(image).run(cpu, 10);
        require_guest_fault(stop, "unaligned branch category");
        test::require(stop.fault_address == kCode + 2 &&
                          stop.fault_width == 4 &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::execute &&
                          stop.raw_instruction.has_value() &&
                          *stop.raw_instruction == 0x48000001 &&
                          cpu.pc == kCode && cpu.lr == 0xCAFEBABE &&
                          stop.instruction_count == 1 &&
                          stop.history_size == 1 &&
                          stop.history[0] == kCode,
                      "unaligned branch hard stop retains and traces word");
    }
}

void test_missing_and_registered_hle_dispatch() {
    {
        auto image = make_image({0, 0x48000001});
        image.branch_overrides.emplace(kCode + 4, kImport);
        image.imports.emplace(kImport,
                              nwii::runtime::ImportTarget{"coreinit",
                                                           "OSGetCurrentThread"});
        CPUContext cpu;
        cpu.pc = kCode + 4;
        for (uint32_t index = 0; index < 8; ++index) {
            cpu.gpr[index + 3] = 0x10 + index;
        }
        const auto stop = Executor(image).run(cpu, 10);
        test::require(stop.category == StopCategory::missing_hle,
                      "missing import category");
        test::require(stop.reason == "unimplemented Cafe import" &&
                          stop.module == "coreinit" &&
                          stop.symbol == "OSGetCurrentThread",
                      "missing import identity and reason");
        test::require(stop.pc == kImport && stop.lr == kCode + 8 &&
                          stop.instruction_count == 1,
                      "missing import call state");
        test::require(stop.argument_gprs[0] == 0x10 &&
                          stop.argument_gprs[7] == 0x17,
                      "missing import argument snapshot");
    }
    {
        auto image = make_image({0x48000000});
        image.imports.emplace(kImport,
                              nwii::runtime::ImportTarget{"coreinit", "Return"});
        CPUContext cpu;
        cpu.pc = kImport;
        cpu.lr = kCode;
        Executor executor(image);
        executor.register_hle(kImport, returning_hle);
        const auto stop = executor.run(cpu, 1);
        test::require(cpu.gpr[3] == 0x12345678 && cpu.pc == kCode &&
                          stop.category == StopCategory::instruction_budget &&
                          stop.history_size == 1 && stop.history[0] == kCode,
                      "registered HLE returns through LR without tracing import");
    }
    {
        ExecutionImage image;
        image.imports.emplace(kImport,
                              nwii::runtime::ImportTarget{"coreinit", "Exit"});
        CPUContext cpu;
        cpu.pc = kImport;
        Executor executor(image);
        executor.register_hle(kImport, exit_hle);
        const auto stop = executor.run(cpu, 10);
        test::require(stop.category == StopCategory::guest_exit &&
                          cpu.gpr[3] == 0x55 && stop.instruction_count == 0,
                      "guest exit handler produces guest_exit stop");
    }
}

void test_slice_boundaries() {
    {
        auto image = make_image({0x38630001});
        CPUContext cpu;
        cpu.pc = kCode;
        const auto slice = Executor(image).run_slice(cpu, 1);
        test::require(slice.category == SliceCategory::quantum &&
                          !slice.terminal.has_value() &&
                          cpu.instruction_count == 1 && cpu.gpr[3] == 1,
                      "slice quantum is nonterminal");
    }
    {
        ExecutionImage image;
        image.imports.emplace(
            kImport, nwii::runtime::ImportTarget{"coreinit", "Wait"});
        CPUContext cpu;
        cpu.pc = kImport;
        cpu.lr = kCode;
        Executor executor(image);
        executor.register_hle(kImport, rescheduling_hle);
        const auto slice = executor.run_slice(cpu, 1);
        test::require(slice.category == SliceCategory::reschedule &&
                          !slice.terminal.has_value() &&
                          cpu.pc == kImport && cpu.lr == kCode &&
                          cpu.instruction_count == 0,
                      "reschedule preserves blocked import and LR");
    }
    {
        ExecutionImage image;
        image.imports.emplace(
            kImport, nwii::runtime::ImportTarget{"coreinit", "Exit"});
        CPUContext cpu;
        cpu.pc = kImport;
        Executor executor(image);
        executor.register_hle(kImport, exit_hle);
        const auto slice = executor.run_slice(cpu, 1);
        test::require(slice.category == SliceCategory::terminal &&
                          slice.terminal.has_value() &&
                          slice.terminal->category ==
                              StopCategory::guest_exit &&
                          cpu.pc == kImport,
                      "exit action is terminal without LR return");
    }
    {
        ExecutionImage image;
        image.imports.emplace(
            kImport, nwii::runtime::ImportTarget{"coreinit", "LegacyExit"});
        CPUContext cpu;
        cpu.pc = kImport;
        cpu.lr = kCode;
        Executor executor(image);
        executor.register_hle(
            kImport, [](CPUContext& context, nwii::runtime::GuestMemory&) {
                context.running = false;
                return HleAction::return_to_lr;
            });
        const auto slice = executor.run_slice(cpu, 1);
        test::require(slice.category == SliceCategory::terminal &&
                          slice.terminal->category ==
                              StopCategory::guest_exit &&
                          cpu.pc == kImport,
                      "existing running flag exit preserves import PC");
    }
}

void test_dispatch_precedence_and_native_thunks() {
    {
        ExecutionImage image;
        image.imports.emplace(kImport,
                              nwii::runtime::ImportTarget{"coreinit", "Missing"});
        CPUContext cpu;
        cpu.pc = kImport;
        Executor executor(image);
        executor.register_native(kImport, 1, native_exit);
        const auto stop = executor.run(cpu, 10);
        test::require(stop.category == StopCategory::missing_hle &&
                          cpu.gpr[4] == 0 && stop.instruction_count == 0,
                      "import dispatch precedes native dispatch");
    }
    {
        ExecutionImage image;
        CPUContext cpu;
        cpu.pc = 0x12340000;
        Executor executor(image);
        executor.register_native(cpu.pc, 1, native_exit);
        test::require(executor.native_dispatch_count() == 0,
                      "native dispatch count starts at zero");
        test::require(executor.native_fallback_count() == 0,
                      "native fallback count starts at zero");
        const auto stop = executor.run(cpu, 10);
        test::require(stop.category == StopCategory::guest_exit &&
                          cpu.gpr[4] == 0xDEADBEEF &&
                          stop.instruction_count == 1 &&
                          stop.history[0] == 0x12340000,
                      "native thunk precedes interpreter fetch and owns trace");
        test::require(executor.native_dispatch_count() == 1,
                      "native thunk dispatch increments count");
        test::require(executor.native_fallback_count() == 0,
                      "native dispatch does not count as fallback");
        cpu = {};
        cpu.pc = 0x12340000;
        (void)executor.run(cpu, 10);
        test::require(executor.native_dispatch_count() == 2,
                      "native dispatch count is monotonic");
    }

    {
        auto image = make_image({0x38630001});
        CPUContext cpu;
        cpu.pc = kCode;
        Executor executor(image);
        executor.register_native(kCode, 2, native_exit);
        const auto stop = executor.run(cpu, 1);
        test::require(stop.category == StopCategory::instruction_budget &&
                          cpu.gpr[3] == 1 && cpu.gpr[4] == 0 &&
                          cpu.instruction_count == 1,
                      "oversized native thunk falls back to interpretation");
        test::require(executor.native_dispatch_count() == 0 &&
                          executor.native_fallback_count() == 1,
                      "oversized native thunk records interpreter fallback");
    }

    {
        ExecutionImage image;
        Executor executor(image);
        test::require_throws(
            [&] { executor.register_native(0x12340000, 0, native_exit); },
            "positive", "zero-instruction native thunk rejected");
    }
    {
        auto image = make_image({0x48000004, 0x60000000});
        CPUContext cpu;
        cpu.pc = kCode;
        Executor executor(image);
        patch_hits = 0;
        executor.register_native(kCode, 1, chained_native);
        executor.register_patch(kCode + 4, patched_exit);
        const auto stop = executor.run(cpu, 10);
        test::require(stop.category == StopCategory::guest_exit &&
                          patch_hits == 1 && cpu.gpr[4] == 0xBEEF &&
                          cpu.gpr[5] == 0 && stop.instruction_count == 2,
                      "chained native yields to runtime patch exactly once");
    }

}

void test_budget_and_history_ring() {
    std::vector<uint32_t> words(40, 0x38630001); // addi r3,r3,1
    ExecutionImage image;
    std::vector<uint8_t> bytes;
    bytes.reserve(words.size() * 4);
    for (const uint32_t word : words) {
        bytes.push_back(static_cast<uint8_t>(word >> 24));
        bytes.push_back(static_cast<uint8_t>(word >> 16));
        bytes.push_back(static_cast<uint8_t>(word >> 8));
        bytes.push_back(static_cast<uint8_t>(word));
    }
    image.memory.map(kCode, static_cast<uint32_t>(bytes.size()),
                     {true, false, true}, bytes);
    CPUContext cpu;
    cpu.pc = kCode;
    const auto stop = Executor(image).run(cpu, 40);
    test::require(stop.category == StopCategory::instruction_budget &&
                      stop.instruction_count == 40 && cpu.gpr[3] == 40 &&
                      stop.history_size == 32,
                  "instruction budget stop and saturated history");
    for (uint32_t index = 0; index < 32; ++index) {
        test::require(stop.history[index] == kCode + (index + 8) * 4,
                      "history linearized oldest to newest after wrap");
    }
}
void test_startup_decoder_fields() {
    constexpr nwiiu::analyzer::PpcInstruction instruction{
        rlwinm(4, 3, 7, 8, 23)};
    static_assert(instruction.uimm() == 0x3A2E);
    static_assert(instruction.bf() == 1);
    static_assert(instruction.sh() == 7);
    static_assert(instruction.mb() == 8);
    static_assert(instruction.me() == 23);
    constexpr nwiiu::analyzer::PpcInstruction fsub{
        x_form(63, 2, 3, 4, 20)};
    static_assert(fsub.xo5() == 20);
}

void test_startup_integer_immediates() {
    struct Case {
        uint32_t word;
        uint32_t source;
        uint32_t initial_xer;
        uint32_t register_index;
        uint32_t expected_register;
        uint32_t cr_index;
        uint8_t expected_cr;
        uint32_t expected_xer;
    };
    constexpr std::array cases{
        Case{d_form(7, 3, 4, 0xFFFE), 0x80000000, 0, 3, 0, 0, 0, 0},
        Case{d_form(10, 8, 4, 0xFFFF), 0x00010000, 0x80000000, 4,
             0x00010000, 2, 0x5, 0x80000000},
        Case{d_form(8, 3, 4, 5), 3, 0x80000000, 3, 2, 0, 0,
             0xA0000000},
        Case{d_form(11, 4, 4, 0xFFFF), 0x80000000, 0x80000000, 4,
             0x80000000, 1, 0x9, 0x80000000},
        Case{d_form(12, 3, 4, 0xFFFF), 0, 0xA0000000, 3, 0xFFFFFFFF, 0,
             0, 0x80000000},
        Case{d_form(13, 3, 4, 1), 0xFFFFFFFF, 0x80000000, 3, 0, 0, 0x3,
             0xA0000000},
        Case{d_form(14, 3, 0, 0x8000), 0xDEADBEEF, 0, 3, 0xFFFF8000, 0,
             0, 0},
        Case{d_form(15, 3, 0, 0xFFFF), 0xDEADBEEF, 0, 3, 0xFFFF0000, 0,
             0, 0},
        Case{d_form(24, 4, 3, 0x00F0), 0xA500000F, 0, 3, 0xA50000FF, 0,
             0, 0},
        Case{d_form(25, 4, 3, 0xFFFF), 0xA50000FF, 0, 3, 0xFFFF00FF, 0,
             0, 0},
        Case{d_form(26, 4, 3, 0xFFFF), 0xA50000FF, 0, 3, 0xA500FF00, 0,
             0, 0},
        Case{d_form(27, 4, 3, 0x8000), 0xA50000FF, 0, 3, 0x250000FF, 0,
             0, 0},
        Case{d_form(28, 4, 3, 0x0017), 0xA5000017, 0x80000000, 3,
             0x00000017, 0, 0x5, 0x80000000},
    };

    for (const auto& test_case : cases) {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[4] = test_case.source;
        cpu.xer = test_case.initial_xer;
        step_word(test_case.word, cpu, image);
        test::require(cpu.gpr[test_case.register_index] ==
                          test_case.expected_register &&
                          cpu.cr[test_case.cr_index] ==
                              test_case.expected_cr &&
                          cpu.xer == test_case.expected_xer &&
                          cpu.pc == kCode + 4,
                      "table-driven immediate semantics");
    }

    auto image = make_image({0});
    CPUContext cpu;
    cpu.gpr[4] = 0x81234567;
    cpu.xer = 0x80000000;
    step_word(rlwinm(4, 3, 8, 8, 23, true), cpu, image);
    test::require(cpu.gpr[3] == 0x00456700 && cpu.cr[0] == 0x5,
                  "rlwinm rotation, mask, and record semantics");
    cpu.gpr[4] = 0x11223344;
    cpu.gpr[3] = 0xAABBCCDD;
    step_word(rlwimi(4, 3, 16, 8, 15), cpu, image);
    test::require(cpu.gpr[3] == 0xAA44CCDD,
                  "rlwimi inserts only the selected rotated bits");
}

void test_startup_opcode31() {
    struct Case {
        uint32_t word;
        uint32_t ra;
        uint32_t rb;
        uint32_t xer;
        uint32_t result_index;
        uint32_t result;
        uint32_t cr_index;
        uint8_t cr;
        uint32_t expected_xer;
    };
    constexpr std::array cases{
        Case{x_form(31, 4, 4, 5, 0), 0x80000000, 1, 0x80000000, 4,
             0x80000000, 1, 0x9, 0x80000000},
        Case{x_form(31, 8, 4, 5, 32), 0xFFFFFFFF, 1, 0x80000000, 4,
             0xFFFFFFFF, 2, 0x5, 0x80000000},
        Case{x_form(31, 3, 4, 5, 40, true), 5, 3, 0x80000000, 3,
             0xFFFFFFFE, 0, 0x9, 0x80000000},
        Case{x_form(31, 3, 4, 5, 136, true), 5, 3, 0xA0000000, 3,
             0xFFFFFFFE, 0, 0x9, 0x80000000},
        Case{x_form(31, 3, 4, 5, 8), 3, 5, 0x80000000, 3, 2, 0, 0,
             0xA0000000},
        Case{x_form(31, 3, 4, 0, 104, true), 5, 0, 0, 3, 0xFFFFFFFB,
             0, 0x8, 0},
        Case{x_form(31, 4, 3, 5, 60, true), 0xFFFF0000, 0x0F0F0F0F,
             0, 3, 0xF0F00000, 0, 0x8, 0},
        Case{x_form(31, 4, 3, 5, 124, true), 0x0F0F0000, 0x00FF00FF,
             0x80000000, 3, 0xF000FF00, 0, 0x9, 0x80000000},
        Case{x_form(31, 4, 3, 5, 24), 1, 4, 0x80000000, 3,
             16, 0, 0, 0x80000000},
        Case{x_form(31, 4, 3, 5, 536), 0x80000000, 4, 0x80000000, 3,
             0x08000000, 0, 0, 0x80000000},
        Case{x_form(31, 4, 3, 5, 28, true), 0xFFFF00FF, 0x0F0F0F0F,
             0, 3, 0x0F0F000F, 0, 0x4, 0},
        Case{x_form(31, 3, 4, 5, 11), 0xFFFFFFFF, 2, 0x80000000, 3,
             1, 0, 0, 0x80000000},
        Case{x_form(31, 3, 4, 5, 235, true), 0x80000000, 2, 0x80000000,
             3, 0, 0, 0x3, 0x80000000},
        Case{x_form(31, 3, 4, 5, 459), 10, 3, 0x80000000, 3,
             3, 0, 0, 0x80000000},
        Case{x_form(31, 3, 4, 5, 266, true), 0xFFFFFFFF, 1, 0x80000000,
             3, 0, 0, 0x3, 0x80000000},
        Case{x_form(31, 4, 3, 5, 444, true), 0x80000000, 1, 0x80000000,
             3, 0x80000001, 0, 0x9, 0x80000000},
        Case{x_form(31, 4, 3, 4, 824, true), 0x8000000F, 0, 0x80000000,
             3, 0xF8000000, 0, 0x9, 0xA0000000},
        Case{x_form(31, 4, 3, 0, 954, true), 0x00000080, 0,
             0x80000000, 3, 0xFFFFFF80, 0, 0x9, 0x80000000},
    };

    for (const auto& test_case : cases) {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[4] = test_case.ra;
        cpu.gpr[5] = test_case.rb;
        cpu.xer = test_case.xer;
        step_word(test_case.word, cpu, image);
        test::require(cpu.gpr[test_case.result_index] == test_case.result &&
                          cpu.cr[test_case.cr_index] == test_case.cr &&
                          cpu.xer == test_case.expected_xer,
                      "table-driven opcode-31 semantics");
    }
    auto divw_image = make_image({0});
    CPUContext divw_cpu;
    divw_cpu.gpr[4] = static_cast<uint32_t>(-100);
    divw_cpu.gpr[6] = 7;
    divw_cpu.xer = 0x80000000;
    step_word(0x7CE433D6, divw_cpu, divw_image);
    test::require(
        divw_cpu.gpr[7] == static_cast<uint32_t>(-14) &&
            divw_cpu.xer == 0x80000000 && divw_cpu.pc == kCode + 4,
        "reached divw performs signed truncating division");

    auto count_image = make_image({0});
    CPUContext count_cpu;
    count_cpu.gpr[4] = 0x00100000;
    step_word(x_form(31, 4, 3, 0, 26, true), count_cpu, count_image);
    test::require(count_cpu.gpr[3] == 11 && count_cpu.cr[0] == 0x4,
                  "cntlzw counts leading zeroes and records result");
    auto extsh_image = make_image({0});
    CPUContext extsh_cpu;
    extsh_cpu.gpr[9] = 0x12348001;
    step_word(0x7D2B0734, extsh_cpu, extsh_image);
    test::require(extsh_cpu.gpr[11] == 0xFFFF8001,
                  "traced extsh sign-extends the low halfword");
    auto xor_image = make_image({0});
    CPUContext xor_cpu;
    xor_cpu.gpr[11] = 0xFF00FF00;
    xor_cpu.gpr[31] = 0x0FFFF000;
    step_word(0x7D6BFA78, xor_cpu, xor_image);
    test::require(xor_cpu.gpr[11] == 0xF0FF0F00,
                  "traced xor combines both source registers");
    xor_cpu.gpr[11] = 0x80000000;
    xor_cpu.gpr[31] = 0;
    step_word(x_form(31, 11, 11, 31, 316, true), xor_cpu, xor_image);
    test::require(xor_cpu.gpr[11] == 0x80000000 && xor_cpu.cr[0] == 0x8,
                  "xor record form updates CR0");
    auto addc_image = make_image({0});
    CPUContext addc_cpu;
    addc_cpu.gpr[4] = 0xFFFFFFFF;
    addc_cpu.gpr[11] = 2;
    step_word(0x7C845814, addc_cpu, addc_image);
    test::require(addc_cpu.gpr[4] == 1 &&
                      (addc_cpu.xer & 0x20000000) != 0,
                  "traced addc writes sum and carry");
    addc_cpu.gpr[4] = 0x80000000;
    addc_cpu.gpr[11] = 0;
    step_word(x_form(31, 4, 4, 11, 10, true), addc_cpu, addc_image);
    test::require(addc_cpu.cr[0] == 0x8, "addc record form updates CR0");

    struct AddzeCase {
        uint32_t word;
        uint32_t input;
        uint32_t xer;
        uint32_t result;
        uint32_t expected_xer;
        uint8_t cr;
    };
    constexpr std::array addze_cases{
        AddzeCase{0x7C630194, 0xFFFFFFFF, 0xA1234567, 0, 0xA1234567,
                  0x6},
        AddzeCase{x_form(31, 3, 3, 0, 202, true), 0, 0x61234567, 1,
                  0x41234567, 0x4},
        AddzeCase{x_form(31, 3, 3, 0, 714, true), 0x7FFFFFFF,
                  0x21234567, 0x80000000, 0xC1234567, 0x9},
        AddzeCase{x_form(31, 3, 3, 0, 714, true), 0, 0xE1234567, 1,
                  0x81234567, 0x5},
    };
    for (const auto& test_case : addze_cases) {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[3] = test_case.input;
        cpu.xer = test_case.xer;
        cpu.cr[0] = 0x6;
        step_word(test_case.word, cpu, image);
        test::require(cpu.gpr[3] == test_case.result &&
                          cpu.xer == test_case.expected_xer &&
                          cpu.cr[0] == test_case.cr,
                      "addze applies CA, OE, and Rc semantics");
    }
    auto subfze_image = make_image({0});
    CPUContext subfze_cpu;
    subfze_cpu.gpr[0] = 0x12345678;
    subfze_cpu.xer = 0x20000000;
    step_word(0x7CC00191, subfze_cpu, subfze_image);
    test::require(subfze_cpu.gpr[6] == 0xEDCBA988 &&
                      (subfze_cpu.xer & 0x20000000) == 0 &&
                      subfze_cpu.cr[0] == 0x8,
                  "reached subfze computes zero minus RA with carry");

    struct SubfzeoCase {
        uint32_t input;
        uint32_t xer;
        uint32_t result;
        uint32_t expected_xer;
        uint8_t cr;
    };
    constexpr std::array subfzeo_cases{
        SubfzeoCase{0x80000000, 0x21234567, 0x80000000, 0xC1234567,
                    0x9},
        SubfzeoCase{0, 0xE1234567, 0, 0xA1234567, 0x3},
    };
    for (const auto& test_case : subfzeo_cases) {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[3] = test_case.input;
        cpu.xer = test_case.xer;
        cpu.cr[0] = 0x6;
        step_word(x_form(31, 3, 3, 0, 712, true), cpu, image);
        test::require(cpu.gpr[3] == test_case.result &&
                          cpu.xer == test_case.expected_xer &&
                          cpu.cr[0] == test_case.cr,
                      "subfzeo applies CA, OE, sticky SO, and Rc semantics");
    }


    auto reserved_addco = make_image({x_form(31, 4, 4, 11, 522)});
    CPUContext reserved_addco_cpu;
    reserved_addco_cpu.pc = kCode;
    require_guest_fault(Executor(reserved_addco).run(reserved_addco_cpu, 1),
                        "unimplemented addco form rejects");

    constexpr std::array reserved_rb_cases{
        x_form(31, 4, 3, 1, 26),
        x_form(31, 3, 4, 1, 104),
        x_form(31, 9, 11, 1, 922),
        x_form(31, 3, 3, 1, 202),
        x_form(31, 3, 3, 1, 714),
        x_form(31, 3, 3, 1, 200),
        x_form(31, 3, 3, 1, 712),
    };
    for (const uint32_t reserved : reserved_rb_cases) {
        auto image = make_image({reserved});
        CPUContext cpu;
        cpu.pc = kCode;
        require_guest_fault(Executor(image).run(cpu, 1),
                            "cntlzw, neg, and extsh reject nonzero RB");
    }

    struct TrapCase {
        uint32_t to;
        uint32_t a;
        uint32_t b;
    };
    constexpr std::array trap_cases{
        TrapCase{0x10, 0xFFFFFFFF, 0}, TrapCase{0x08, 1, 0},
        TrapCase{0x04, 7, 7},          TrapCase{0x02, 0, 0xFFFFFFFF},
        TrapCase{0x01, 0xFFFFFFFF, 0},
    };
    for (const auto& trap_case : trap_cases) {
        const uint32_t word =
            x_form(31, trap_case.to, 4, 5, 4);
        auto image = make_image({word});
        CPUContext cpu;
        cpu.pc = kCode;
        cpu.gpr[4] = trap_case.a;
        cpu.gpr[5] = trap_case.b;
        require_guest_fault(Executor(image).run(cpu, 1),
                            "tw selected condition traps");
    }

    auto image = make_image({0});
    CPUContext cpu;
    cpu.gpr[4] = 7;
    cpu.gpr[5] = 7;
    step_word(x_form(31, 0x10, 4, 5, 4), cpu, image);
    test::require(cpu.pc == kCode + 4,
                  "tw advances when no selected condition holds");
}

void test_divwo_overflow_and_record_semantics() {
    static_assert(x_form(31, 7, 4, 6, 1003) == 0x7CE437D6);
    static_assert(x_form(31, 7, 4, 6, 1003, true) == 0x7CE437D7);
    struct Case {
        uint32_t word;
        uint32_t dividend;
        uint32_t divisor;
        uint32_t xer;
        uint32_t result;
        uint32_t expected_xer;
        uint8_t initial_cr;
        uint8_t expected_cr;
    };
    constexpr std::array cases{
        Case{0x7CE437D6, static_cast<uint32_t>(-100), 7, 0x41234567,
             static_cast<uint32_t>(-14), 0x01234567, 0x6, 0x6},
        Case{0x7CE437D7, 10, 0, 0x21234567, 0, 0xE1234567, 0x6,
             0x3},
        Case{0x7CE437D7, 0x80000000, 0xFFFFFFFF, 0x01234567,
             0x80000000, 0xC1234567, 0x6, 0x9},
        Case{0x7CE437D7, 9, 3, 0xC1234567, 3, 0x81234567, 0x6, 0x5},
    };

    for (const auto& test_case : cases) {
        auto image = make_image({test_case.word});
        CPUContext cpu;
        cpu.pc = kCode;
        cpu.gpr[4] = test_case.dividend;
        cpu.gpr[6] = test_case.divisor;
        cpu.xer = test_case.xer;
        cpu.cr[0] = test_case.initial_cr;
        Executor(image).step(cpu);
        test::require(cpu.gpr[7] == test_case.result &&
                          cpu.xer == test_case.expected_xer &&
                          cpu.cr[0] == test_case.expected_cr &&
                          cpu.pc == kCode + 4,
                      "divwo handles overflow, sticky SO, and Rc ordering");
    }

    auto reserved = make_image({0x7CE437D4});
    CPUContext cpu;
    cpu.pc = kCode;
    cpu.gpr[7] = 0xA5A5A5A5;
    cpu.xer = 0x81234567;
    require_guest_fault(Executor(reserved).run(cpu, 1),
                        "neighboring reserved divwo encoding rejects");
    test::require(cpu.gpr[7] == 0xA5A5A5A5 && cpu.xer == 0x81234567,
                  "reserved divwo encoding leaves architectural state intact");
}

void test_startup_memory_forms() {
    auto image = make_image({0});
    CPUContext cpu;
    cpu.gpr[1] = kData + 0x20;
    cpu.gpr[2] = 4;
    cpu.gpr[3] = 0xA1B2C3D4;
    image.memory.write32(kData + 0x24, 0x11223344, 0);
    image.memory.write8(kData + 0x28, 0xAB, 0);

    step_word(x_form(31, 4, 1, 2, 23), cpu, image);
    test::require(cpu.gpr[4] == 0x11223344, "lwzx indexed load");
    cpu.gpr[9] = kData + 0x28;
    cpu.gpr[0] = 0;
    step_word(0x7D6900AE, cpu, image);
    test::require(cpu.gpr[11] == 0xAB,
                  "traced lbzx zero-extends an indexed byte");
    auto reserved_lbzx = make_image({uint32_t{0x7D6900AF}});
    CPUContext reserved_lbzx_cpu;
    reserved_lbzx_cpu.pc = kCode;
    reserved_lbzx_cpu.gpr[9] = 0x30000000;
    const auto reserved_lbzx_stop =
        Executor(reserved_lbzx).run(reserved_lbzx_cpu, 1);
    require_guest_fault(reserved_lbzx_stop, "lbzx Rc form is reserved");
    test::require(
        reserved_lbzx_stop.fault_address == kCode &&
            reserved_lbzx_stop.fault_access ==
                nwii::runtime::MemoryAccess::execute,
        "reserved lbzx rejects before guest memory access");
    image.memory.write16(kData + 0x26, 0x8001, 0);
    cpu.gpr[27] = kData + 0x20;
    cpu.gpr[8] = 6;
    step_word(0x7D7B42AE, cpu, image);
    test::require(cpu.gpr[11] == 0xFFFF8001,
                  "traced lhax sign-extends an indexed halfword");

    auto reserved_lhax =
        make_image({x_form(31, 11, 27, 8, 343, true)});
    CPUContext reserved_lhax_cpu;
    reserved_lhax_cpu.pc = kCode;
    const auto reserved_lhax_stop =
        Executor(reserved_lhax).run(reserved_lhax_cpu, 1);
    require_guest_fault(reserved_lhax_stop, "lhax Rc form is reserved");
    test::require(
        reserved_lhax_stop.fault_address == kCode &&
            reserved_lhax_stop.fault_access ==
                nwii::runtime::MemoryAccess::execute,
        "reserved lhax rejects before guest memory access");
    step_word(x_form(31, 3, 1, 2, 151), cpu, image);
    test::require(image.memory.read32(kData + 0x24, 0) == 0xA1B2C3D4,
                  "stwx indexed store");
    cpu.gpr[2] = 0x10;
    step_word(x_form(31, 3, 1, 2, 215), cpu, image);
    test::require(image.memory.read8(kData + 0x30, 0) == 0xD4,
                  "stbx indexed byte store");
    cpu.gpr[7] = kData + 0x40;
    cpu.gpr[27] = 2;
    cpu.gpr[29] = 0xA1B2C3D4;
    step_word(0x7FA7DB2E, cpu, image);
    test::require(image.memory.read16(kData + 0x42, 0) == 0xC3D4,
                  "traced sthx stores an indexed big-endian halfword");

    auto reserved_sthx = make_image({x_form(31, 29, 7, 27, 407, true)});
    CPUContext reserved_sthx_cpu;
    reserved_sthx_cpu.pc = kCode;
    reserved_sthx_cpu.gpr[7] = 0x30000000;
    reserved_sthx_cpu.gpr[27] = 2;
    const auto reserved_sthx_stop =
        Executor(reserved_sthx).run(reserved_sthx_cpu, 1);
    require_guest_fault(reserved_sthx_stop, "sthx Rc form is reserved");
    test::require(
        reserved_sthx_stop.fault_address == kCode &&
            reserved_sthx_stop.fault_access ==
                nwii::runtime::MemoryAccess::execute,
        "reserved sthx rejects before guest memory access");
    cpu.gpr[31] = kData + 0x24;
    step_word(x_form(31, 8, 31, 4, 597), cpu, image);
    test::require(cpu.gpr[8] == 0xA1B2C3D4,
                  "lswi loads an immediate byte count in big-endian order");
    cpu.gpr[7] = kData + 0x50;
    step_word(x_form(31, 8, 7, 4, 725), cpu, image);
    test::require(image.memory.read32(kData + 0x50, 0) == 0xA1B2C3D4,
                  "stswi stores an immediate byte count in big-endian order");

    constexpr std::array invalid_lswi_forms{
        x_form(31, 0, 0, 4, 597),
        x_form(31, 8, 9, 8, 597),
        x_form(31, 31, 1, 12, 597),
    };
    for (const uint32_t reserved : invalid_lswi_forms) {
        auto invalid_image = make_image({reserved});
        CPUContext invalid_cpu;
        invalid_cpu.pc = kCode;
        invalid_cpu.gpr[1] = 0x30000000;
        invalid_cpu.gpr[9] = 0x30000000;
        const auto stop = Executor(invalid_image).run(invalid_cpu, 1);
        require_guest_fault(stop, "reserved lswi form faults");
        test::require(
            stop.fault_address == kCode && stop.fault_width == 4 &&
                stop.fault_access == nwii::runtime::MemoryAccess::execute,
            "lswi overlap, wraparound, and zero forms reject before memory");
    }

    image.memory.write16(kData + 0x34, 0xFF80, 0);
    step_word(d_form(42, 8, 1, 0x14), cpu, image);
    step_word(d_form(40, 9, 1, 0x14), cpu, image);
    test::require(cpu.gpr[9] == 0x0000FF80,
                  "lhz zero-extends halfword");
    test::require(cpu.gpr[8] == 0xFFFFFF80,
                  "lha sign-extends halfword");
    cpu.gpr[1] = kData + 0x20;
    image.memory.write32(kData + 0x2C, 0x55667788, 0);
    step_word(d_form(33, 7, 1, 12), cpu, image);
    test::require(cpu.gpr[7] == 0x55667788 && cpu.gpr[1] == kData + 0x2C,
                  "lwzu load before base update");
    cpu.gpr[1] = kData + 0x20;

    image.memory.write16(kData + 0x2C, 0x8123, 0);
    step_word(d_form(41, 7, 1, 12), cpu, image); // lhzu r7,12(r1)
    test::require(cpu.gpr[7] == 0x00008123 && cpu.gpr[1] == kData + 0x2C,
                  "lhzu zero-extends the halfword before updating base");
    cpu.gpr[1] = kData + 0x20;
    {
        auto reserved = make_image({d_form(41, 7, 7, 2)}); // lhzu r7,2(r7)
        CPUContext reserved_cpu;
        reserved_cpu.pc = kCode;
        reserved_cpu.gpr[7] = kData + 0x40;
        const auto stop = Executor(reserved).run(reserved_cpu, 1);
        require_guest_fault(stop, "lhzu ra==rt form is reserved");
        test::require(stop.fault_address == kCode &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::execute &&
                          reserved_cpu.gpr[7] == kData + 0x40,
                      "reserved lhzu rejects before any base update");
    }
    {
        auto unaligned = make_image({d_form(42, 7, 10, 1)}); // lha r7,1(r10)
        CPUContext unaligned_cpu;
        unaligned_cpu.pc = kCode;
        unaligned_cpu.gpr[10] = kData + 0x40;
        unaligned.memory.write16(kData + 0x41, 0x8123, 0);
        Executor(unaligned).step(unaligned_cpu);
        test::require(unaligned_cpu.gpr[7] == 0xFFFF8123 &&
                          unaligned_cpu.gpr[10] == kData + 0x40,
                      "lha sign-extends the title's packed unaligned fields");
    }

    step_word(d_form(34, 5, 1, 8), cpu, image);
    test::require(cpu.gpr[5] == 0xAB, "lbz");
    step_word(d_form(35, 6, 1, 8), cpu, image);
    test::require(cpu.gpr[6] == 0xAB && cpu.gpr[1] == kData + 0x28,
                  "lbzu load before base update");

    cpu.gpr[1] = kData + 0x20;
    step_word(d_form(38, 3, 1, 9), cpu, image);
    test::require(image.memory.read8(kData + 0x29, 0) == 0xD4, "stb");
    step_word(d_form(39, 3, 1, 10), cpu, image);
    test::require(image.memory.read8(kData + 0x2A, 0) == 0xD4 &&
                      cpu.gpr[1] == kData + 0x2A,
                  "stbu store before base update");

    cpu.gpr[1] = kData + 0x20;
    step_word(d_form(44, 3, 1, 12), cpu, image);
    test::require(image.memory.read16(kData + 0x2C, 0) == 0xC3D4, "sth");
    step_word(d_form(45, 3, 1, 14), cpu, image);
    test::require(image.memory.read16(kData + 0x2E, 0) == 0xC3D4 &&
                      cpu.gpr[1] == kData + 0x2E,
                  "sthu stores before updating base");
    cpu.gpr[1] = kData + 0x20;

    cpu.gpr[1] = kData + 0x40;
    image.memory.write32(kData + 0x40, 0x01020304, 0);
    image.memory.write32(kData + 0x44, 0x05060708, 0);
    step_word(d_form(46, 30, 1, 0), cpu, image);
    test::require(cpu.gpr[30] == 0x01020304 &&
                      cpu.gpr[31] == 0x05060708,
                  "lmw");
    cpu.gpr[30] = 0xCAFEBABE;
    cpu.gpr[31] = 0x0BADF00D;
    step_word(d_form(47, 30, 1, 8), cpu, image);
    test::require(image.memory.read32(kData + 0x48, 0) == 0xCAFEBABE &&
                      image.memory.read32(kData + 0x4C, 0) == 0x0BADF00D,
                  "stmw");

    for (const uint32_t reserved :
         {d_form(33, 3, 0, 0), d_form(33, 3, 3, 0),
          d_form(35, 3, 0, 0), d_form(35, 3, 3, 0),
          d_form(37, 3, 0, 0), d_form(39, 3, 0, 0),
          d_form(45, 3, 0, 0)}) {
        auto invalid_image = make_image({reserved});
        invalid_image.memory.map(0, 4, {true, true, false});
        CPUContext invalid_cpu;
        invalid_cpu.pc = kCode;
        const auto stop = Executor(invalid_image).run(invalid_cpu, 1);
        require_guest_fault(stop, "reserved update form faults before access");
        test::require(invalid_image.memory.read32(0, 0) == 0,
                      "reserved update form leaves memory unchanged");
    }
}
void test_whole_wwhd_opcode31_forms() {
    {
        auto image = make_image({0});
        CPUContext cpu;
        for (uint32_t field = 0; field < cpu.cr.size(); ++field) {
            cpu.cr[field] = static_cast<uint8_t>(field + 1);
        }
        step_word(0x7D800026, cpu, image); // mfcr r12
        test::require(cpu.gpr[12] == 0x12345678,
                      "exact WWHD mfcr packs CR0 through CR7");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[0] = 0x80000000;
        cpu.gpr[8] = 2;
        step_word(0x7D404096, cpu, image); // mulhw r10,r0,r8
        test::require(cpu.gpr[10] == 0xFFFFFFFF,
                      "exact WWHD mulhw returns signed high word");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[7] = kData + 0x20;
        cpu.gpr[8] = 4;
        image.memory.write16(kData + 0x24, 0x8123, 0);
        step_word(0x7C87422E, cpu, image); // lhzx r4,r7,r8
        test::require(cpu.gpr[4] == 0x8123,
                      "exact WWHD lhzx zero-extends indexed halfword");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[9] = kData + 0x20;
        cpu.gpr[10] = 6;
        image.memory.write16(kData + 0x26, 0xFEDC, 0);
        step_word(0x7C09526E, cpu, image); // lhzux r0,r9,r10
        test::require(cpu.gpr[0] == 0xFEDC && cpu.gpr[9] == kData + 0x26,
                      "exact WWHD lhzux loads before base update");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[12] = kData + 0x20;
        cpu.gpr[26] = 8;
        image.memory.write32(kData + 0x28, std::bit_cast<uint32_t>(1.25F), 0);
        step_word(0x7CECD42E, cpu, image); // lfsx f7,r12,r26
        test::require(lane0(cpu, 7) == 1.25,
                      "exact WWHD lfsx promotes indexed guest float");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[12] = kData + 0x20;
        cpu.gpr[11] = 12;
        image.memory.write32(kData + 0x2C, std::bit_cast<uint32_t>(-2.5F), 0);
        step_word(0x7DAC5C6E, cpu, image); // lfsux f13,r12,r11
        test::require(lane0(cpu, 13) == -2.5 &&
                          cpu.gpr[12] == kData + 0x2C,
                      "exact WWHD lfsux loads before base update");
    }
}
void test_whole_wwhd_second_wave_forms() {
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.cr[1] = 0;
        step_word(0x4CC63242, cpu, image); // creqv 6,6,6
        test::require(cpu.cr[1] == 0x2,
                      "exact WWHD creqv writes complemented equivalence");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[28] = 0x80000001;
        cpu.gpr[4] = 1;
        step_word(0x5F84203E, cpu, image); // rlwnm r4,r28,r4,0,31
        test::require(cpu.gpr[4] == 3,
                      "exact WWHD rlwnm rotates by register");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[10] = 0xF0FFFFFF;
        step_word(0x75491028, cpu, image); // andis. r9,r10,0x1028
        test::require(cpu.gpr[9] == 0x10280000 && cpu.cr[0] == 0x4,
                      "exact WWHD andis masks high immediate and records");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[8] = kData + 0x240;
        image.memory.write16(kData + 0x28, 0xFF80, 0);
        step_word(0xACE8FDE8, cpu, image); // lhau r7,-536(r8)
        test::require(cpu.gpr[7] == 0xFFFFFF80 &&
                          cpu.gpr[8] == kData + 0x28,
                      "exact WWHD lhau sign-extends before base update");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[7] = kData + 0x27;
        for (uint32_t offset = 0x20; offset < 0x40; offset += 4) {
            image.memory.write32(kData + offset, 0xFFFFFFFF, 0);
        }
        step_word(0x7C003FEC, cpu, image); // dcbz 0,r7
        bool zero = true;
        for (uint32_t offset = 0x20; offset < 0x40; offset += 4) {
            zero &= image.memory.read32(kData + offset, 0) == 0;
        }
        test::require(zero, "exact WWHD dcbz clears its 32-byte cache line");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[9] = kData + 0x20;
        cpu.gpr[4] = 4;
        cpu.fpr[9][0] = double_bits(1.5);
        step_word(0x7D29256E, cpu, image); // stfsux f9,r9,r4
        test::require(
            image.memory.read32(kData + 0x24, 0) ==
                    std::bit_cast<uint32_t>(1.5F) &&
                cpu.gpr[9] == kData + 0x24,
            "exact WWHD stfsux stores before base update");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[9] = 0x80000001;
        cpu.gpr[0] = 1;
        step_word(0x7D2A0630, cpu, image); // sraw r10,r9,r0
        test::require(cpu.gpr[10] == 0xC0000000 &&
                          (cpu.xer & 0x20000000) != 0,
                      "exact WWHD sraw sign-fills and records discarded one");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[12] = kData + 0x20;
        cpu.gpr[27] = 8;
        image.memory.write32(kData + 0x28, 0x12345678, 0);
        step_word(0x7D6CD86E, cpu, image); // lwzux r11,r12,r27
        test::require(cpu.gpr[11] == 0x12345678 &&
                          cpu.gpr[12] == kData + 0x28,
                      "exact WWHD lwzux loads before base update");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.gpr[5] = 0x89ABCDEF;
        cpu.gpr[27] = kData + 0x20;
        cpu.gpr[10] = 12;
        step_word(0x7CBB516E, cpu, image); // stwux r5,r27,r10
        test::require(image.memory.read32(kData + 0x2C, 0) == 0x89ABCDEF &&
                          cpu.gpr[27] == kData + 0x2C,
                      "exact WWHD stwux stores before base update");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.fpr[13][0] = double_bits(2.5);
        step_word(0xFC006910, cpu, image); // fnabs f0,f13
        test::require(lane0(cpu, 0) == -2.5,
                      "exact WWHD fnabs forces the sign bit");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.fpr[13][0] = double_bits(3.0);
        cpu.fpr[27][0] = double_bits(2.0);
        step_word(0xFD6D6EF8, cpu, image); // fmsub f11,f13,f13,f27
        test::require(lane0(cpu, 11) == 3.0,
                      "exact WWHD fmsub performs fused A*C-B");
    }
    {
        constexpr std::array words{
            uint32_t{0x7D62E3A6}, uint32_t{0x7D83E3A6},
            uint32_t{0x7C04E3A6}, uint32_t{0x7C05E3A6},
        };
        for (const uint32_t word : words) {
            auto image = make_image({0});
            CPUContext cpu;
            step_word(word, cpu, image);
            test::require(cpu.pc == kCode + 4,
                          "exact WWHD implementation-control mtspr executes");
        }
    }
}


void test_startup_float_forms() {
    auto image = make_image({0});
    CPUContext cpu;
    cpu.gpr[1] = kData;
    image.memory.write32(kData, std::bit_cast<uint32_t>(1.5F), 0);
    image.memory.write64(kData + 8, 0x7FF8123456789ABC, 0);

    step_word(d_form(48, 2, 1, 0), cpu, image);
    test::require(lane0(cpu, 2) == 1.5, "lfs promotes guest float");
    image.memory.write32(kData + 4, std::bit_cast<uint32_t>(2.5F), 0);
    step_word(d_form(49, 1, 1, 4), cpu, image);
    test::require(lane0(cpu, 1) == 2.5 && cpu.gpr[1] == kData + 4,
                  "lfsu loads before updating base");
    cpu.gpr[1] = kData;
    step_word(d_form(50, 3, 1, 8), cpu, image);
    test::require(cpu.fpr[3][0] == 0x7FF8123456789ABC, "lfd raw payload");

    cpu.fpr[4][0] = double_bits(1.0 / 3.0);
    step_word(d_form(52, 4, 1, 16), cpu, image);
    test::require(image.memory.read32(kData + 16, 0) ==
                      std::bit_cast<uint32_t>(static_cast<float>(1.0 / 3.0)),
                  "stfs rounds to guest float");
    step_word(d_form(53, 4, 1, 32), cpu, image);
    test::require(
        cpu.gpr[1] == kData + 32 &&
            image.memory.read32(kData + 32, 0) ==
                std::bit_cast<uint32_t>(static_cast<float>(1.0 / 3.0)),
        "stfsu stores before updating base");
    cpu.gpr[1] = kData;
    cpu.fpr[5][0] = 0xFFF8123456789ABC;
    step_word(d_form(54, 5, 1, 24), cpu, image);
    test::require(image.memory.read64(kData + 24, 0) ==
                      0xFFF8123456789ABC,
                  "stfd preserves lane-0 payload");
    cpu.gpr[9] = kData - 0x468;
    cpu.fpr[13][0] = 0x0123456789ABCDEF;
    step_word(0xDDA904E8, cpu, image);
    test::require(cpu.gpr[9] == kData + 0x80 &&
                      image.memory.read64(kData + 0x80, 0) ==
                          0x0123456789ABCDEF,
                  "traced stfdu stores lane 0 before updating base");

    cpu.gpr[27] = kData + 0x40;
    cpu.gpr[0] = 8;
    cpu.fpr[30][0] = double_bits(3.25);
    step_word(x_form(31, 30, 27, 0, 663), cpu, image); // stfsx f30,r27,r0
    test::require(image.memory.read32(kData + 0x48, 0) ==
                      std::bit_cast<uint32_t>(3.25F),
                  "stfsx rounds the indexed store to a guest single");

    auto reserved_stfsx = make_image({x_form(31, 30, 27, 0, 663, true)});
    CPUContext reserved_stfsx_cpu;
    reserved_stfsx_cpu.pc = kCode;
    reserved_stfsx_cpu.gpr[27] = 0x30000000;
    const auto reserved_stfsx_stop =
        Executor(reserved_stfsx).run(reserved_stfsx_cpu, 1);
    require_guest_fault(reserved_stfsx_stop, "stfsx Rc form is reserved");
    test::require(reserved_stfsx_stop.fault_address == kCode &&
                      reserved_stfsx_stop.fault_access ==
                          nwii::runtime::MemoryAccess::execute,
                  "reserved stfsx rejects before guest memory access");

    image.memory.write32(kData + 0x50, std::bit_cast<uint32_t>(1.25F), 0);
    image.memory.write32(kData + 0x54, std::bit_cast<uint32_t>(-4.5F), 0);
    cpu.gpr[7] = kData + 0x50;
    cpu.fpr[7][0] = 0;
    cpu.fpr[7][1] = 0;
    step_word(0xE0E70000, cpu, image); // psq_l f7,0(r7),0,GQR0
    test::require(lane0(cpu, 7) == 1.25 &&
                      std::bit_cast<double>(cpu.fpr[7][1]) == -4.5,
                  "psq_l loads a float32 pair into both paired-single lanes");
    step_word(0xE0E78000, cpu, image); // psq_l f7,0(r7),W=1,GQR0
    test::require(lane0(cpu, 7) == 1.25 &&
                      std::bit_cast<double>(cpu.fpr[7][1]) == 1.0,
                  "psq_l W=1 loads one element and sets the second lane to 1.0");
    {
        auto quantized = make_image({0xE0E71000}); // psq_l with I=1 (GQR1)
        CPUContext quantized_cpu;
        quantized_cpu.pc = kCode;
        quantized_cpu.gpr[7] = kData;
        const auto stop = Executor(quantized).run(quantized_cpu, 1);
        require_guest_fault(stop, "non-zero psq_l GQR index is unsupported");
        test::require(stop.fault_address == kCode &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::execute,
                      "unmodeled psq_l GQR rejects before guest memory access");
    }

    auto reserved_stfdu = make_image({d_form(55, 13, 0, 8)});
    reserved_stfdu.memory.map(0, 8, {true, true, false});
    CPUContext reserved_stfdu_cpu;
    reserved_stfdu_cpu.pc = kCode;
    reserved_stfdu_cpu.fpr[13][0] = 0xFFFFFFFFFFFFFFFF;
    const auto reserved_stfdu_stop =
        Executor(reserved_stfdu).run(reserved_stfdu_cpu, 1);
    require_guest_fault(reserved_stfdu_stop, "stfdu RA-zero form is reserved");
    test::require(reserved_stfdu.memory.read64(0, 0) == 0,
                  "reserved stfdu rejects before guest memory access");
    cpu.fpr[30][0] = double_bits(1.5);
    cpu.fpr[11][0] = double_bits(-2.0);
    step_word(0xFC1E02F2, cpu, image);
    test::require(lane0(cpu, 0) == -3.0,
                  "traced fmul multiplies double-precision operands");

    for (const uint32_t reserved :
         {a_form(63, 0, 30, 1, 11, 25), uint32_t{0xFC1E02F3}}) {
        auto reserved_fmul = make_image({reserved});
        CPUContext reserved_fmul_cpu;
        reserved_fmul_cpu.pc = kCode;
        require_guest_fault(Executor(reserved_fmul).run(reserved_fmul_cpu, 1),
                            "reserved fmul form faults");
    }
    cpu.fpr[1][0] = 0xFFF8123456789ABC;
    step_word(0xFC200A10, cpu, image);
    test::require(cpu.fpr[1][0] == 0x7FF8123456789ABC,
                  "traced fabs clears only the double sign bit");

    for (const uint32_t reserved :
         {x_form(63, 1, 1, 1, 264), uint32_t{0xFC200A11}}) {
        auto reserved_fabs = make_image({reserved});
        CPUContext reserved_fabs_cpu;
        reserved_fabs_cpu.pc = kCode;
        require_guest_fault(Executor(reserved_fabs).run(reserved_fabs_cpu, 1),
                            "reserved fabs form faults");
    }
    cpu.fpr[30][0] = double_bits(2.5);
    cpu.fpscr = 0;
    step_word(0xFFC0F01C, cpu, image);
    test::require((cpu.fpr[30][0] & 0xFFFFFFFF) == 2,
                  "traced fctiw rounds ties to nearest even");
    cpu.fpr[30][0] = double_bits(2.1);
    cpu.fpscr = 2;
    step_word(0xFFC0F01C, cpu, image);
    test::require((cpu.fpr[30][0] & 0xFFFFFFFF) == 3,
                  "fctiw honors the FPSCR positive-infinity mode");

    for (const uint32_t reserved :
         {x_form(63, 30, 1, 30, 14), uint32_t{0xFFC0F01D}}) {
        auto reserved_fctiw = make_image({reserved});
        CPUContext reserved_fctiw_cpu;
        reserved_fctiw_cpu.pc = kCode;
        require_guest_fault(
            Executor(reserved_fctiw).run(reserved_fctiw_cpu, 1),
            "reserved fctiw form faults");
    }
    cpu.fpr[0][0] = double_bits(1.0 + 0x1p-27);
    cpu.fpr[12][0] = double_bits(1.0 - 0x1p-27);
    cpu.fpr[13][0] = double_bits(-1.0);
    step_word(0xFDA06B3A, cpu, image);
    test::require(lane0(cpu, 13) == -0x1p-54,
                  "traced fmadd performs one fused double rounding");

    auto reserved_fmadd = make_image({uint32_t{0xFDA06B3B}});
    CPUContext reserved_fmadd_cpu;
    reserved_fmadd_cpu.pc = kCode;
    require_guest_fault(Executor(reserved_fmadd).run(reserved_fmadd_cpu, 1),
                        "fmadd record form is outside the supported subset");
    cpu.fpr[9][0] = double_bits(1.0 + 0x1p-27);
    cpu.fpr[8][0] = double_bits(1.0 - 0x1p-27);
    cpu.fpr[13][0] = double_bits(1.0);
    step_word(0xFD496A3C, cpu, image);
    test::require(lane0(cpu, 10) == 0x1p-54,
                  "traced fnmsub performs one fused double rounding");
    auto reserved_fnmsub = make_image({uint32_t{0xFD496A3D}});
    CPUContext reserved_fnmsub_cpu;
    reserved_fnmsub_cpu.pc = kCode;
    require_guest_fault(Executor(reserved_fnmsub).run(reserved_fnmsub_cpu, 1),
                        "fnmsub record form is reserved");

    // Exact reached fnmsubs f18,f13,f23,f18 (0xEE4D95FC): opcode 59, XO5 30,
    // Rc 0. fD = round_single(-(fA*fC - fB)) with a single fused rounding.
    cpu.fpr[13][0] = double_bits(1.0 + 0x1p-27);
    cpu.fpr[23][0] = double_bits(1.0 - 0x1p-27);
    cpu.fpr[18][0] = double_bits(1.0);
    step_word(0xEE4D95FC, cpu, image);
    test::require(lane0(cpu, 18) == 0x1p-54,
                  "traced fnmsubs fuses the multiply-subtract before rounding");
    cpu.fpr[13][0] = double_bits(1.0);
    cpu.fpr[23][0] = double_bits(0.1);
    cpu.fpr[18][0] = double_bits(0.0);
    step_word(0xEE4D95FC, cpu, image);
    test::require(lane0(cpu, 18) ==
                      static_cast<double>(static_cast<float>(-0.1)),
                  "fnmsubs rounds the fused negative result to single");
    auto reserved_fnmsubs = make_image({uint32_t{0xEE4D95FD}});
    CPUContext reserved_fnmsubs_cpu;
    reserved_fnmsubs_cpu.pc = kCode;
    require_guest_fault(
        Executor(reserved_fnmsubs).run(reserved_fnmsubs_cpu, 1),
        "fnmsubs record form is reserved");

    cpu.fpr[6][0] = double_bits(5.25);
    cpu.fpr[7][0] = double_bits(2.0);
    cpu.fpr[10][0] = double_bits(3.0);
    step_word(a_form(59, 8, 6, 7, 10, 29), cpu, image);
    test::require(lane0(cpu, 8) ==
                      static_cast<double>(static_cast<float>(17.75)),
                  "fmadds uses FRC and one single-precision result");
    step_word(a_form(59, 8, 6, 0, 10, 25), cpu, image);
    test::require(lane0(cpu, 8) ==
                      static_cast<double>(static_cast<float>(15.75)),
                  "fmuls uses FRC and single rounding");
    step_word(x_form(59, 8, 6, 7, 18), cpu, image);
    test::require(lane0(cpu, 8) ==
                      static_cast<double>(static_cast<float>(2.625)),
                  "fdivs uses XO5 and single rounding");
    step_word(x_form(59, 8, 6, 7, 21), cpu, image);
    test::require(lane0(cpu, 8) ==
                      static_cast<double>(static_cast<float>(7.25)),
                  "fadds uses XO5 and single rounding");
    step_word(x_form(59, 8, 6, 7, 20), cpu, image);
    test::require(lane0(cpu, 8) ==
                      static_cast<double>(static_cast<float>(3.25)),
                  "fsubs uses XO5 and single rounding");
    step_word(x_form(63, 9, 6, 7, 20), cpu, image);
    test::require(lane0(cpu, 9) == 3.25, "fsub uses XO5");
    cpu.fpr[10][0] = double_bits(1.25);
    cpu.fpr[13][0] = double_bits(2.5);
    step_word(0xFDAA682A, cpu, image);
    test::require(lane0(cpu, 13) == 3.75,
                  "traced fadd adds double-precision operands");
    for (const uint32_t reserved :
         {a_form(63, 13, 10, 13, 1, 21), uint32_t{0xFDAA682B}}) {
        auto reserved_image = make_image({reserved});
        CPUContext reserved_cpu;
        reserved_cpu.pc = kCode;
        require_guest_fault(Executor(reserved_image).run(reserved_cpu, 1),
                            "fadd reserved FRC and record forms reject");
    }
    cpu.fpr[0][0] = double_bits(2.5);
    cpu.fpr[1][0] = double_bits(7.5);
    step_word(0xFC210024, cpu, image);
    test::require(lane0(cpu, 1) == 3.0,
                  "traced fdiv divides double-precision operands");
    for (const uint32_t reserved :
         {a_form(63, 1, 1, 0, 1, 18), uint32_t{0xFC210025}}) {
        auto reserved_image = make_image({reserved});
        CPUContext reserved_cpu;
        reserved_cpu.pc = kCode;
        require_guest_fault(Executor(reserved_image).run(reserved_cpu, 1),
                            "fdiv reserved FRC and record forms reject");
    }
    step_word(x_form(63, 10, 0, 6, 12), cpu, image);
    test::require(lane0(cpu, 10) ==
                      static_cast<double>(static_cast<float>(5.25)),
                  "frsp rounds through float");
    step_word(x_form(63, 11, 0, 6, 15), cpu, image);
    test::require((cpu.fpr[11][0] & 0xFFFFFFFF) == 5, "fctiwz payload");
    cpu.fpr[6][0] =
        double_bits(std::numeric_limits<double>::quiet_NaN());
    step_word(x_form(63, 11, 0, 6, 15), cpu, image);
    test::require(cpu.fpr[11][0] == 0x80000000,
                  "fctiwz avoids undefined NaN conversion");
    cpu.fpr[6][0] = 0x7FF8123456789ABC;
    step_word(x_form(63, 12, 0, 6, 72), cpu, image);
    test::require(cpu.fpr[12][0] == 0x7FF8123456789ABC,
                  "fmr raw payload copy");
    step_word(x_form(63, 13, 0, 6, 40), cpu, image);
    test::require(cpu.fpr[13][0] == 0xFFF8123456789ABC,
                  "fneg toggles only the raw sign bit");
    step_word(x_form(63, 8, 6, 7, 0), cpu, image);
    test::require(cpu.cr[2] == 0x1, "fcmpu unordered CR field");

    cpu.fpr[13] = {0x1111111111111111, 0x2222222222222222};
    cpu.fpr[14] = {0x3333333333333333, 0x4444444444444444};
    step_word(x_form(4, 13, 13, 14, 592), cpu, image);
    test::require(cpu.fpr[13][0] == 0x2222222222222222 &&
                      cpu.fpr[13][1] == 0x3333333333333333,
                  "ps_merge10 aliases safely and copies raw lanes");

    cpu.fpr[13] = {0x1111111111111111, 0x2222222222222222};
    cpu.fpr[14] = {0x3333333333333333, 0x4444444444444444};
    step_word(x_form(4, 12, 13, 14, 624), cpu, image); // ps_merge11 f12,f13,f14
    test::require(cpu.fpr[12][0] == 0x2222222222222222 &&
                      cpu.fpr[12][1] == 0x4444444444444444,
                  "ps_merge11 copies raw high lanes of frA and frB");

    cpu.fpr[13] = {0x1111111111111111, 0x2222222222222222};
    cpu.fpr[14] = {0x3333333333333333, 0x4444444444444444};
    step_word(x_form(4, 12, 13, 14, 560), cpu, image); // ps_merge01 f12,f13,f14
    test::require(cpu.fpr[12][0] == 0x1111111111111111 &&
                      cpu.fpr[12][1] == 0x4444444444444444,
                  "ps_merge01 copies frA low lane and frB high lane");

    cpu.fpr[10] = {double_bits(2.0), double_bits(-3.0)};
    step_word(0x11405050, cpu, image); // ps_neg f10,f10
    test::require(lane0(cpu, 10) == -2.0 &&
                      std::bit_cast<double>(cpu.fpr[10][1]) == 3.0,
                  "ps_neg toggles the sign bit of both paired-single lanes");

    cpu.fpr[7] = {double_bits(2.0), double_bits(-3.0)};
    cpu.fpr[22] = {double_bits(1.5), double_bits(99.0)};
    step_word(0x11A70598, cpu, image); // ps_muls0 f13,f7,f22
    test::require(lane0(cpu, 13) == 3.0 &&
                      std::bit_cast<double>(cpu.fpr[13][1]) == -4.5,
                  "ps_muls0 scales both lanes by frC ps0 and ignores frC ps1");

    cpu.fpr[2] = {double_bits(2.0), double_bits(-3.0)};
    step_word(0x104200B2, cpu, image); // ps_mul f2,f2,f2
    test::require(lane0(cpu, 2) == 4.0 &&
                      std::bit_cast<double>(cpu.fpr[2][1]) == 9.0,
                  "ps_mul multiplies paired-single lanes independently");

    cpu.fpr[3] = {double_bits(2.0), double_bits(-1.5)};
    cpu.fpr[2] = {double_bits(0.5), double_bits(10.0)};
    step_word(0x102310FA, cpu, image); // ps_madd f1,f3,f3,f2
    test::require(lane0(cpu, 1) == 4.5 &&
                      std::bit_cast<double>(cpu.fpr[1][1]) == 12.25,
                  "ps_madd fuses multiply-add per paired-single lane");

    cpu.fpr[1] = {double_bits(3.0), double_bits(99.0)};
    cpu.fpr[2] = {double_bits(7.0), double_bits(5.0)};
    step_word(0x10211094, cpu, image); // ps_sum0 f1,f1,f2,f2
    test::require(lane0(cpu, 1) == 8.0 &&
                      std::bit_cast<double>(cpu.fpr[1][1]) == 5.0,
                  "ps_sum0 sums frA0+frB1 into lane0 and copies frC1 to lane1");

    cpu.fpr[7] = {double_bits(2.0), double_bits(-3.0)};
    cpu.fpr[9] = {double_bits(5.0), double_bits(1.5)};
    step_word(0x1007482A, cpu, image); // ps_add f0,f7,f9
    test::require(lane0(cpu, 0) == 7.0 &&
                      std::bit_cast<double>(cpu.fpr[0][1]) == -1.5,
                  "ps_add sums paired-single lanes independently");

    cpu.fpr[13] = {double_bits(2.0), double_bits(-3.0)};
    cpu.fpr[24] = {double_bits(5.0), double_bits(1.5)};
    step_word(0x116DC028, cpu, image); // ps_sub f11,f13,f24
    test::require(lane0(cpu, 11) == -3.0 &&
                      std::bit_cast<double>(cpu.fpr[11][1]) == -4.5,
                  "ps_sub subtracts paired-single lanes independently");

    cpu.fpr[0] = {double_bits(1.0 + 0x1p-27), double_bits(1.0 + 0x1p-27)};
    cpu.fpr[3] = {double_bits(1.0 - 0x1p-27), double_bits(1.0 - 0x1p-27)};
    cpu.fpr[4] = {double_bits(1.0), double_bits(1.0)};
    step_word(0x10A020F8, cpu, image); // ps_msub f5,f0,f3,f4 (frA=0,frC=3,frB=4)
    // Fused fA*fC-fB is exactly -2^-54; an unfused double multiply rounds
    // fA*fC to 1.0 and flushes the difference to zero, so this fails if fma is
    // replaced by a plain multiply-then-subtract.
    test::require(lane0(cpu, 5) == -0x1p-54 &&
                      std::bit_cast<double>(cpu.fpr[5][1]) == -0x1p-54,
                  "ps_msub fuses each lane before single rounding");

    cpu.fpr[1][0] = double_bits(4.0);
    step_word(0xFC800834, cpu, image); // frsqrte f4,f1
    test::require(std::bit_cast<uint64_t>(lane0(cpu, 4)) ==
                      0x3FDFFE8000000000ULL,
                  "frsqrte matches the Espresso reciprocal-sqrt estimate table");
    cpu.fpr[1][0] = double_bits(0.0);
    step_word(0xFC800834, cpu, image);
    test::require(lane0(cpu, 4) == std::numeric_limits<double>::infinity(),
                  "frsqrte of +0 is +infinity");
    cpu.fpr[1][0] = double_bits(-4.0);
    step_word(0xFC800834, cpu, image);
    test::require(lane0(cpu, 4) != lane0(cpu, 4),
                  "frsqrte of a negative operand is NaN");
    cpu.fpr[1][0] = double_bits(std::numeric_limits<double>::infinity());
    step_word(0xFC800834, cpu, image);
    test::require(lane0(cpu, 4) == 0.0, "frsqrte of +infinity is +0");

    cpu.fpr[9][0] = double_bits(3.0);
    cpu.fpr[1][0] = double_bits(4.0);
    cpu.fpr[10][0] = double_bits(5.0);
    step_word(0xEC095078, cpu, image); // fmsubs f0,f9,f1,f10
    test::require(lane0(cpu, 0) == 7.0,
                  "fmsubs computes frA*frC - frB rounded to single");

    cpu.gpr[2] = 32;
    cpu.fpr[11][0] = 0xABCDEF1287654321;
    step_word(x_form(31, 11, 1, 2, 983), cpu, image);
    test::require(image.memory.read32(kData + 32, 0) == 0x87654321,
                  "stfiwx stores low raw lane bits");

    cpu.fpr[12][0] = double_bits(3.0);
    cpu.fpr[26][0] = double_bits(42.0);
    step_word(0xFF6CD32E, cpu, image); // fsel f27,f12,f26,f12
    test::require(lane0(cpu, 27) == 3.0,
                  "fsel selects frC when frA is non-negative");
    cpu.fpr[12][0] = double_bits(-1.0);
    step_word(0xFF6CD32E, cpu, image);
    test::require(lane0(cpu, 27) == 42.0,
                  "fsel selects frB when frA is negative");
    cpu.fpr[12][0] = double_bits(std::numeric_limits<double>::quiet_NaN());
    cpu.fpr[26][0] = double_bits(-13.0);
    step_word(0xFF6CD32E, cpu, image);
    test::require(lane0(cpu, 27) == -13.0,
                  "fsel selects frB when frA is unordered");

    cpu.gpr[1] = kData + 0x60;
    cpu.fpr[13] = {double_bits(2.5), double_bits(-1.25)};
    image.memory.write32(kData + 0x74, 0xDEADBEEF, 0);
    step_word(0xF1A10010, cpu, image); // psq_st f13,16(r1),0,GQR0
    test::require(image.memory.read32(kData + 0x70, 0) ==
                          std::bit_cast<uint32_t>(2.5F) &&
                      image.memory.read32(kData + 0x74, 0) ==
                          std::bit_cast<uint32_t>(-1.25F),
                  "psq_st writes a float32 pair from both paired-single lanes");
    image.memory.write32(kData + 0x74, 0xDEADBEEF, 0);
    step_word(0xF1A18010, cpu, image); // psq_st f13,16(r1),W=1,GQR0
    test::require(image.memory.read32(kData + 0x70, 0) ==
                          std::bit_cast<uint32_t>(2.5F) &&
                      image.memory.read32(kData + 0x74, 0) == 0xDEADBEEF,
                  "psq_st W=1 stores only the first lane");
    {
        auto quantized = make_image({0xF1A11010}); // psq_st with I=1 (GQR1)
        CPUContext quantized_cpu;
        quantized_cpu.pc = kCode;
        quantized_cpu.gpr[1] = kData;
        const auto stop = Executor(quantized).run(quantized_cpu, 1);
        require_guest_fault(stop, "non-zero psq_st GQR index is unsupported");
        test::require(stop.fault_address == kCode &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::execute,
                      "unmodeled psq_st GQR rejects before guest memory access");
    }
}

void test_startup_control_and_faults() {
    {
        auto image = make_image({xl_form(0, 0, 150)});
        CPUContext cpu;
        cpu.pc = kCode;
        Executor(image).step(cpu);
        test::require(cpu.pc == kCode + 4, "isync advances as synchronization no-op");
    }
    {
        auto image = make_image({0x41820008});
        image.branch_overrides.emplace(kCode, kImport);
        CPUContext cpu;
        cpu.pc = kCode;
        cpu.cr[0] = 0x2;
        Executor(image).step(cpu);
        test::require(cpu.pc == kImport, "opcode-16 relocation override");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.lr = kCode + 0x43;
        step_word(xl_form(20, 0, 16), cpu, image);
        test::require(cpu.pc == kCode + 0x40, "bclr masks LR low bits");
        cpu.ctr = kCode + 0x47;
        step_word(xl_form(20, 0, 528), cpu, image);
        test::require(cpu.pc == kCode + 0x44, "bcctr masks CTR low bits");
    }
    constexpr std::array reserved_encodings{
        d_form(10, 5, 3, 1), d_form(10, 6, 3, 1),
        d_form(11, 5, 3, 1), d_form(11, 6, 3, 1),
        x_form(31, 5, 3, 5, 0), x_form(31, 6, 3, 5, 0),
        x_form(31, 5, 3, 5, 32), x_form(31, 6, 3, 5, 32),
        x_form(19, 1, 0, 0, 150), x_form(19, 0, 1, 0, 150),
        x_form(19, 0, 0, 1, 150),
        x_form(19, 20, 0, 4, 16), x_form(19, 20, 0, 8, 16),
        x_form(19, 20, 0, 16, 16),
        x_form(19, 20, 0, 4, 528), x_form(19, 20, 0, 8, 528),
        x_form(19, 20, 0, 16, 528),
        x_form(63, 3, 1, 5, 12), x_form(63, 3, 1, 5, 15),
        x_form(63, 3, 1, 5, 72),
        x_form(63, 5, 3, 5, 0), x_form(63, 6, 3, 5, 0),
        x_form(63, 4, 3, 5, 0, true),
        x_form(59, 3, 4, 5, 20, true),
        a_form(59, 3, 4, 5, 6, 25),
        a_form(59, 3, 4, 5, 6, 18),
        a_form(59, 3, 4, 5, 6, 20),
        a_form(59, 3, 4, 5, 6, 21),
        x_form(63, 3, 0, 5, 12, true),
        x_form(63, 3, 0, 5, 15, true),
        x_form(63, 3, 4, 5, 20, true),
        x_form(63, 3, 0, 5, 72, true),
        x_form(4, 1, 2, 3, 592, true),
        x_form(4, 12, 13, 14, 560, true), // ps_merge01 record bit reserved
        x_form(4, 12, 13, 14, 624, true), // ps_merge11 record bit reserved
        x_form(4, 10, 0, 10, 40, true),   // ps_neg record bit reserved
        x_form(4, 10, 5, 10, 40),         // ps_neg nonzero FRA reserved
        a_form(4, 5, 0, 4, 3, 21),        // ps_add nonzero FRC reserved
        a_form(4, 5, 0, 4, 3, 20),        // ps_sub nonzero FRC reserved
    };
    for (const uint32_t reserved : reserved_encodings) {
        auto image = make_image({reserved});
        CPUContext cpu;
        cpu.pc = kCode;
        require_guest_fault(Executor(image).run(cpu, 1),
                            "reserved Task 2 encoding remains a hard fault");
    }

    {
        auto image = make_image({x_form(31, 1, 2, 3, 1)});
        CPUContext cpu;
        cpu.pc = kCode;
        require_guest_fault(Executor(image).run(cpu, 1),
                            "unsupported forms remain hard faults");
    }
}
void test_ps_sum1_operand_lanes() {
    {
        CPUContext cpu;
        cpu.fpr[2][0] = double_bits(1.0 + 0x1p-24);
        cpu.fpr[3][0] = double_bits(1.0 + 0x1p-30);
        cpu.fpr[4][1] = double_bits(0x1p-24);
        nwii::runtime::ppc::ps_sum1(cpu, 1, 2, 3, 4);
        test::require(
            lane0(cpu, 1) == 1.0 &&
                std::bit_cast<double>(cpu.fpr[1][1]) ==
                    1.0 + 0x1p-23,
            "ps_sum1 single-rounds copied and summed lanes");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.fpr[0] = {
            double_bits(1.0 + 0x1p-24),
            double_bits(0x1p-24),
        };
        cpu.fpr[7][0] = double_bits(1.0 + 0x1p-30);
        step_word(0x100001D6, cpu, image);
        test::require(
            lane0(cpu, 0) == 1.0 &&
                std::bit_cast<double>(cpu.fpr[0][1]) ==
                    1.0 + 0x1p-23,
            "exact reached ps_sum1 word single-rounds both output lanes");
    }
    {
        auto image = make_image({0});
        CPUContext cpu;
        cpu.fpr[5] = {
            double_bits(1.0 + 0x1p-24),
            double_bits(2.0 + 0x1p-23),
        };
        cpu.fpr[3][1] = double_bits(1.0 + 0x1p-23);
        step_word(0x118500DA, cpu, image);
        test::require(
            lane0(cpu, 12) == 1.0 + 0x1p-22 &&
                std::bit_cast<double>(cpu.fpr[12][1]) == 2.0 + 0x1p-21,
            "exact reached ps_muls1 multiplies in double then rounds once");
    }
    {
        auto image = make_image({spr_form(3, 898, 339)});
        CPUContext cpu;
        cpu.pc = kCode;
        require_guest_fault(Executor(image).run(cpu, 1),
                            "mfspr 898 remains unsupported");
    }
    for (const uint32_t word :
         {x_form(31, 3, 4, 1, 26), x_form(31, 3, 4, 1, 104)}) {
        auto image = make_image({word});
        CPUContext cpu;
        cpu.pc = kCode;
        const auto stop = Executor(image).run(cpu, 1);
        require_guest_fault(stop, "cntlzw/neg reserved RB remains rejected");
        test::require(stop.fault_address == kCode &&
                          stop.fault_access ==
                              nwii::runtime::MemoryAccess::execute,
                      "cntlzw/neg reserved RB rejects before execution");
    }
}
} // namespace


int main() {
    test_decoder_fields();
    test_integer_memory_and_spr_instructions();
    test_reserved_stwu();
    test_opcode31_low_bit();
    test_unaligned_word_faults();
    test_direct_and_conditional_branches();
    test_indirect_branches();
    test_crxor_condition_register_logic();
    test_branch_override();
    test_stops_and_failed_instruction_accounting();
    test_missing_and_registered_hle_dispatch();
    test_slice_boundaries();
    test_dispatch_precedence_and_native_thunks();
    test_budget_and_history_ring();
    test_startup_decoder_fields();
    test_startup_integer_immediates();
    test_startup_opcode31();
    test_divwo_overflow_and_record_semantics();
    test_startup_memory_forms();
    test_whole_wwhd_opcode31_forms();
    test_whole_wwhd_second_wave_forms();
    test_startup_float_forms();
    test_ps_sum1_operand_lanes();
    test_startup_control_and_faults();
}
