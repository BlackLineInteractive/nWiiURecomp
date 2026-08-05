#include "runtime/executor.h"

#include "nwiiu/analyzer/ppc_instruction.h"
#include "runtime/ppc_semantics.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <unordered_map>

namespace nwii::runtime {
namespace {
using nwiiu::analyzer::PpcInstruction;

ExecutionStop make_stop(StopCategory category, const char* reason,
                        const CPUContext& cpu) {
    ExecutionStop stop;
    stop.category = category;
    stop.reason = reason;
    stop.pc = cpu.pc;
    stop.lr = cpu.lr;
    stop.instruction_count = cpu.instruction_count;
    stop.history_size = cpu.history_size;
    const size_t oldest =
        (cpu.history_cursor + cpu.pc_history.size() - cpu.history_size) %
        cpu.pc_history.size();
    for (size_t index = 0; index < cpu.history_size; ++index) {
        stop.history[index] =
            cpu.pc_history[(oldest + index) % cpu.pc_history.size()];
    }
    return stop;
}

ExecutionStop make_guest_fault_stop(const GuestFault& fault,
                                    const CPUContext& cpu) {
    auto stop =
        make_stop(StopCategory::guest_fault, fault.what(), cpu);
    stop.pc = fault.pc;
    stop.fault_address = fault.address;
    stop.fault_width = fault.width;
    stop.fault_access = fault.access;
    stop.raw_instruction = cpu.current_instruction;
    return stop;
}
} // namespace

// Recompiled blocks do not record the raw instruction word on the hot path:
// that cost a store on every guest instruction for a field only a fault report
// reads. Recover it from the image at the faulting PC instead.
ExecutionStop Executor::with_faulting_word(ExecutionStop stop) const {
    // A failed fetch has no instruction context by definition: the word could
    // not be read, which is the fault. Only data faults get the word back.
    if (!stop.raw_instruction.has_value() &&
        stop.fault_access != MemoryAccess::execute) {
        try {
            stop.raw_instruction = image_.memory.read32(stop.pc, stop.pc);
        } catch (const GuestFault&) {
            // leave it absent rather than reporting a second fault
        }
    }
    return stop;
}

Executor::Executor(ExecutionImage& image)
    : image_(image), native_pages_(0x10000) {}

void Executor::register_native(uint32_t address, uint64_t instruction_count,
                               NativeThunk thunk) {
    if (instruction_count == 0) {
        throw std::invalid_argument(
            "native thunk instruction count must be positive");
    }
    if (image_.imports.contains(address) ||
        patched_addresses_.contains(address)) {
        return;
    }
    if ((address & 3) != 0) {
        unaligned_native_thunks_.insert_or_assign(
            address, RegisteredNative{instruction_count, thunk});
        return;
    }
    auto& page = native_pages_[address >> 16];
    if (page == nullptr) {
        page = std::make_unique<NativePage>();
    }
    (*page)[(address & 0xFFFF) >> 2] = {instruction_count, thunk};
}

void Executor::register_patch(uint32_t address, NativeThunk thunk) {
    if ((address & 3) != 0) {
        throw std::invalid_argument("patch address must be word aligned");
    }
    if (!patched_addresses_.insert(address).second) {
        throw std::invalid_argument("patch address already registered");
    }
    auto& page = native_pages_[address >> 16];
    if (page == nullptr) {
        page = std::make_unique<NativePage>();
    }
    (*page)[(address & 0xFFFF) >> 2] = {1, thunk};
}

void Executor::register_hle(uint32_t address, HleHandler handler) {
    if (!hle_handlers_.emplace(address, std::move(handler)).second) {
        throw std::invalid_argument("HLE address already registered");
    }
}

size_t Executor::trace_enter(uint32_t address, uint32_t thread_id,
                             const ImportTarget& import) {
    if (!trace_enabled_) {
        return kHleTraceCapacity;
    }
    const size_t slot = hle_trace_cursor_;
    hle_trace_[slot] =
        {address, thread_id, import.module, import.symbol, false};
    hle_trace_cursor_ = (hle_trace_cursor_ + 1) % kHleTraceCapacity;
    if (hle_trace_size_ < kHleTraceCapacity) {
        ++hle_trace_size_;
    } else {
        hle_trace_truncated_ = true;
    }
    return slot;
}

void Executor::snapshot_trace(ExecutionStop& stop,
                              uint32_t active_thread) const {
    stop.active_thread = active_thread;
    if (!trace_enabled_) {
        return;
    }
    stop.hle_call_count = hle_trace_size_;
    stop.hle_trace_truncated = hle_trace_truncated_;
    const size_t oldest =
        (hle_trace_cursor_ + kHleTraceCapacity - hle_trace_size_) %
        kHleTraceCapacity;
    for (size_t index = 0; index < hle_trace_size_; ++index) {
        stop.hle_calls[index] =
            hle_trace_[(oldest + index) % kHleTraceCapacity];
    }
}

[[noreturn]] void Executor::unsupported(uint32_t pc, const char* reason) {
    throw GuestFault(reason, pc, 4, pc, MemoryAccess::execute);
}

void Executor::step(CPUContext& cpu) {
    cpu.current_instruction.reset();
    const uint32_t pc = cpu.pc;
    const uint32_t raw = image_.memory.fetch32(pc);

    ppc::trace_instruction(cpu, raw);
    const PpcInstruction instruction{raw};
    const uint32_t opcode = instruction.opcode();
    const uint32_t xo = instruction.extended_opcode();

    switch (opcode) {
    case 4:
        if (instruction.rc()) {
            unsupported(pc, "reserved paired-single record bit");
        }
        switch (instruction.xo5()) {
        case 12: // ps_muls0
        case 13: // ps_muls1
        case 25: // ps_mul
            if (instruction.rb() != 0) {
                unsupported(pc, "reserved paired-single FRB field");
            }
            break;
        case 20: // ps_sub
        case 21: // ps_add
            if (instruction.frc() != 0) {
                unsupported(pc, "reserved paired-single FRC field");
            }
            break;
        case 10: // ps_sum0
        case 11: // ps_sum1
        case 14: // ps_madds0
        case 15: // ps_madds1
        case 23: // ps_sel
        case 28: // ps_msub
        case 29: // ps_madd
            break;
        case 30: // ps_nmsub
        case 31: // ps_nmadd
            break;
        default:
            if (xo == 26 && instruction.ra() == 0) { // ps_rsqrte
                break;
            }
            if (xo == 32) { // ps_cmpo0
                if ((instruction.rt() & 3) != 0) {
                    unsupported(pc, "reserved paired-single compare bits");
                }
            } else if (xo == 40) { // ps_neg
                if (instruction.ra() != 0) {
                    unsupported(pc, "reserved paired-single FRA field");
                }
            } else if (xo != 528 && xo != 560 && xo != 592 && xo != 624) {
                unsupported(pc, "unsupported PowerPC instruction");
            }
        }
        break;
    case 7:
        break;
    case 8:
        break;
    case 10:
    case 11:
        if ((instruction.rt() & 0x3) != 0) {
            unsupported(pc, "reserved compare bits");
        }
        break;
    case 12:
    case 13:
    case 14:
    case 15:
    case 16:
    case 18:
    case 20:
    case 21:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 23:
    case 29:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 39:
    case 40:
    case 41:
    case 42:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 49:
    case 50:
    case 52:
    case 53:
    case 54:
    case 55:
        break;
    case 43:
        if (instruction.ra() == 0 || instruction.ra() == instruction.rt()) {
            unsupported(pc, "reserved update load form");
        }
        break;
    case 57:
        if (instruction.ra() == 0 || instruction.ps_i() != 0) {
            unsupported(pc, "reserved paired-single update load form");
        }
        break;
    case 56:
    case 60:
        if (instruction.ps_i() == 1 || instruction.ps_i() > 5) {
            unsupported(pc, "unsupported paired-single GQR index");
        }
        break;
    case 61:
        if (instruction.ra() == 0 || instruction.ps_i() != 0) {
            unsupported(pc, "reserved paired-single update store form");
        }
        break;
    case 19:
        if (xo != 16 && xo != 150 && xo != 193 && xo != 289 && xo != 528) {
            unsupported(pc, "unsupported PowerPC instruction");
        }
        if (xo == 150 &&
            (instruction.bo() != 0 || instruction.bi() != 0 ||
             instruction.rb() != 0 || instruction.link())) {
            unsupported(pc, "reserved isync bits");
        }
        if ((xo == 193 || xo == 289) && instruction.link()) {
            unsupported(pc, "reserved condition-register logic bit");
        }
        if ((xo == 16 || xo == 528) &&
            (instruction.rb() & 0x1C) != 0) {
            unsupported(pc, "reserved branch hint bits");
        }
        if (xo == 528 && (instruction.bo() & 0x04) == 0) {
            unsupported(pc, "bcctr cannot decrement CTR");
        }
        break;
    case 31:
        switch (xo) {
        case 0:
        case 4:
        case 23:
        case 87:
        case 32:
        case 151:
        case 343:
        case 215:
        case 407:
        case 339:
        case 467:
        case 597:
        case 725:
        case 663:
        case 983:
        case 19:
        case 279:
        case 311:
        case 535:
        case 567:
        case 55:
        case 183:
        case 695:
        case 1014:
        case 20:
        case 54:
        case 86:
        case 119:
        case 247:
        case 375:
        case 439:
        case 534:
        case 662:
            if (instruction.rc() ||
                ((xo == 0 || xo == 32) &&
                 (instruction.rt() & 0x3) != 0)) {
                unsupported(pc, "reserved opcode-31 bit");
            }
            break;
        case 24:
        case 26:
            break;
        case 10:
        case 11:
        case 75:
        case 8:
        case 28:
        case 40:
        case 60:
        case 104:
        case 124:
        case 136:
        case 138:
        case 412:
        case 200:
        case 712:
        case 202:
        case 714:
        case 235:
        case 536:
        case 266:
        case 316:
        case 444:
        case 824:
        case 792:
        case 459:
        case 491:
        case 1003:
        case 922:
        case 954:
            break;
        case 150:
            if (!instruction.rc()) {
                unsupported(pc, "stwcx. requires record bit");
            }
            break;
        default:
            unsupported(pc, "unsupported PowerPC instruction");
        }
        if ((xo == 26 || xo == 104 || xo == 922 || xo == 954) &&
            instruction.rb() != 0) {
            unsupported(pc, "reserved opcode-31 RB field");
        }
        if ((xo == 200 || xo == 202 || xo == 712 || xo == 714) &&
            instruction.rb() != 0) {
            unsupported(pc, "reserved zero-operand RB field");
        }
        if (xo == 19 &&
            (instruction.ra() != 0 || instruction.rb() != 0)) {
            unsupported(pc, "reserved mfcr field");
        }
        if (xo == 311 &&
            (instruction.ra() == 0 || instruction.ra() == instruction.rt())) {
            unsupported(pc, "reserved update load form");
        }
        if (xo == 567 && instruction.ra() == 0) {
            unsupported(pc, "reserved update load form");
        }
        if (xo == 55 &&
            (instruction.ra() == 0 || instruction.ra() == instruction.rt())) {
            unsupported(pc, "reserved update load form");
        }
        if ((xo == 183 || xo == 695) && instruction.ra() == 0) {
            unsupported(pc, "reserved update store form");
        }
        if ((xo == 119 || xo == 375) &&
            (instruction.ra() == 0 || instruction.ra() == instruction.rt())) {
            unsupported(pc, "reserved update load form");
        }
        if ((xo == 247 || xo == 439) && instruction.ra() == 0) {
            unsupported(pc, "reserved update store form");
        }
        if ((xo == 54 || xo == 86) && instruction.rt() != 0) {
            unsupported(pc, "reserved cache instruction field");
        }
        if (xo == 1014 && instruction.rt() != 0) {
            unsupported(pc, "reserved dcbz field");
        }
        if (xo == 339 && instruction.spr() != 8 &&
            instruction.spr() != 9) {
            unsupported(pc, "unsupported SPR");
        }
        if (xo == 467 && instruction.spr() != 8 &&
            instruction.spr() != 9 &&
            (instruction.spr() < 898 || instruction.spr() > 901)) {
            unsupported(pc, "unsupported SPR");
        }
        if (xo == 597 &&
            !ppc::valid_lswi_form(instruction.rt(), instruction.ra(),
                                  instruction.rb())) {
            unsupported(pc, "reserved lswi register overlap");
        }
        break;
    case 59:
        if (((instruction.xo5() != 18 && instruction.xo5() != 20 &&
              instruction.xo5() != 21 && instruction.xo5() != 25 &&
              instruction.xo5() != 28 && instruction.xo5() != 29 &&
              instruction.xo5() != 30) &&
             xo != 24) ||
            instruction.rc()) {
            unsupported(pc, "unsupported PowerPC instruction");
        }
        if ((instruction.xo5() == 18 || instruction.xo5() == 20 ||
             instruction.xo5() == 21) &&
            instruction.frc() != 0) {
            unsupported(pc, "reserved binary floating-point FRC field");
        }
        if (instruction.xo5() == 25 && instruction.rb() != 0) {
            unsupported(pc, "reserved fmuls FRB field");
        }
        if (xo == 24 && instruction.ra() != 0) {
            unsupported(pc, "reserved fres FRA field");
        }
        break;
    case 63:
        if (instruction.rc()) {
            unsupported(pc, "reserved floating-point record bit");
        }
        if (instruction.xo5() == 18 && instruction.frc() == 0) {
            break;
        }
        if (instruction.xo5() == 20) {
            break;
        }
        if (instruction.xo5() == 21 && instruction.frc() == 0) {
            break;
        }
        if (instruction.xo5() == 25 && instruction.rb() == 0) {
            break;
        }
        if (instruction.xo5() == 29) {
            break;
        }
        if (instruction.xo5() == 30) {
            break;
        }
        if (instruction.xo5() == 28) {
            break;
        }
        if (instruction.xo5() == 23) {
            break;
        }
        if (xo == 0) {
            if ((instruction.rt() & 0x3) != 0) {
                unsupported(pc, "reserved floating-point compare bits");
            }
            break;
        }
        if ((xo == 136 || xo == 264) && instruction.ra() == 0) {
            break;
        }
        if (xo == 14 && instruction.ra() == 0) {
            break;
        }
        if (xo == 26 && instruction.ra() == 0) {
            break;
        }
        if ((xo == 12 || xo == 15 || xo == 40 || xo == 72) &&
            instruction.ra() == 0) {
            break;
        }
        unsupported(pc, "unsupported PowerPC instruction");
        break;
    default:
        unsupported(pc, "unsupported PowerPC instruction");
    }

    switch (opcode) {
    case 4:
        if (xo == 528) {
            ppc::ps_merge00(cpu, instruction.rt(), instruction.ra(),
                            instruction.rb());
        } else if (xo == 592) {
            ppc::ps_merge10(cpu, instruction.rt(), instruction.ra(),
                            instruction.rb());
        } else if (xo == 560) {
            ppc::ps_merge01(cpu, instruction.rt(), instruction.ra(),
                            instruction.rb());
        } else if (xo == 624) {
            ppc::ps_merge11(cpu, instruction.rt(), instruction.ra(),
                            instruction.rb());
        } else if (xo == 40) {
            ppc::ps_neg(cpu, instruction.rt(), instruction.rb());
        } else if (xo == 32) {
            ppc::ps_cmpo0(cpu, instruction.bf(), instruction.ra(),
                          instruction.rb());
        } else if (xo == 26) {
            ppc::ps_rsqrte(cpu, instruction.rt(), instruction.rb());
        } else if (instruction.xo5() == 25) {
            ppc::ps_mul(cpu, instruction.rt(), instruction.ra(),
                        instruction.frc());
        } else if (instruction.xo5() == 29) {
            ppc::ps_madd(cpu, instruction.rt(), instruction.ra(),
                         instruction.frc(), instruction.rb());
        } else if (instruction.xo5() == 10) {
            ppc::ps_sum0(cpu, instruction.rt(), instruction.ra(),
                         instruction.frc(), instruction.rb());
        } else if (instruction.xo5() == 11) {
            ppc::ps_sum1(cpu, instruction.rt(), instruction.ra(),
                         instruction.frc(), instruction.rb());
        } else if (instruction.xo5() == 14) {
            ppc::ps_madds0(cpu, instruction.rt(), instruction.ra(),
                           instruction.frc(), instruction.rb());
        } else if (instruction.xo5() == 15) {
            ppc::ps_madds1(cpu, instruction.rt(), instruction.ra(),
                           instruction.frc(), instruction.rb());
        } else if (instruction.xo5() == 23) {
            ppc::ps_sel(cpu, instruction.rt(), instruction.ra(),
                        instruction.frc(), instruction.rb());
        } else if (instruction.xo5() == 21) {
            ppc::ps_add(cpu, instruction.rt(), instruction.ra(),
                        instruction.rb());
        } else if (instruction.xo5() == 20) {
            ppc::ps_sub(cpu, instruction.rt(), instruction.ra(),
                        instruction.rb());
        } else if (instruction.xo5() == 28) {
            ppc::ps_msub(cpu, instruction.rt(), instruction.ra(),
                         instruction.frc(), instruction.rb());
        } else if (instruction.xo5() == 13) {
            ppc::ps_muls1(cpu, instruction.rt(), instruction.ra(),
                          instruction.frc());
        } else if (instruction.xo5() == 30) {
            ppc::ps_nmsub(cpu, instruction.rt(), instruction.ra(),
                          instruction.frc(), instruction.rb());
        } else if (instruction.xo5() == 31) {
            ppc::ps_nmadd(cpu, instruction.rt(), instruction.ra(),
                          instruction.frc(), instruction.rb());
        } else {
            ppc::ps_muls0(cpu, instruction.rt(), instruction.ra(),
                          instruction.frc());
        }
        return;
    case 7:
        ppc::mulli(cpu, instruction.rt(), instruction.ra(),
                   instruction.simm());
        return;
    case 8:
        ppc::subfic(cpu, instruction.rt(), instruction.ra(),
                    instruction.simm());
        return;
    case 10:
        ppc::cmpli(cpu, instruction.bf(), instruction.ra(),
                   instruction.uimm());
        return;
    case 11:
        ppc::cmpi(cpu, instruction.bf(), instruction.ra(),
                  instruction.simm());
        return;
    case 12:
    case 13:
        ppc::addic(cpu, instruction.rt(), instruction.ra(),
                   instruction.simm(), opcode == 13);
        return;
    case 14:
        ppc::addi(cpu, instruction.rt(), instruction.ra(), instruction.simm());
        return;
    case 15:
        ppc::addis(cpu, instruction.rt(), instruction.ra(),
                   instruction.simm());
        return;
    case 16: {
        const auto override = image_.branch_overrides.find(pc);
        const uint32_t target = override == image_.branch_overrides.end()
                                    ? instruction.branch_target(pc)
                                    : override->second;
        ppc::bc(cpu, target, instruction.bo(), instruction.bi(),
                instruction.link());
        return;
    }
    case 18: {
        const auto override = image_.branch_overrides.find(pc);
        const uint32_t target = override == image_.branch_overrides.end()
                                    ? instruction.branch_target(pc)
                                    : override->second;
        ppc::b(cpu, target, instruction.link());
        return;
    }
    case 19:
        if (xo == 16) {
            ppc::bclr(cpu, instruction.bo(), instruction.bi(),
                      instruction.link());
        } else if (xo == 528) {
            ppc::bcctr(cpu, instruction.bo(), instruction.bi(),
                       instruction.link());
        } else if (xo == 193) {
            ppc::crxor(cpu, instruction.bo(), instruction.bi(),
                       instruction.rb());
        } else if (xo == 289) {
            ppc::creqv(cpu, instruction.bo(), instruction.bi(),
                       instruction.rb());
        } else {
            ppc::isync(cpu);
        }
        return;
    case 20:
        ppc::rlwimi(cpu, instruction.ra(), instruction.rs(),
                    instruction.sh(), instruction.mb(), instruction.me(),
                    instruction.rc());
        return;
    case 21:
        ppc::rlwinm(cpu, instruction.ra(), instruction.rs(),
                    instruction.sh(), instruction.mb(), instruction.me(),
                    instruction.rc());
        return;
    case 23:
        ppc::rlwnm(cpu, instruction.ra(), instruction.rs(), instruction.rb(),
                   instruction.mb(), instruction.me(), instruction.rc());
        return;
    case 24:
        ppc::ori(cpu, instruction.ra(), instruction.rs(), instruction.uimm());
        return;
    case 25:
        ppc::ori(cpu, instruction.ra(), instruction.rs(),
                 instruction.uimm() << 16);
        return;
    case 26:
        ppc::xori(cpu, instruction.ra(), instruction.rs(),
                  instruction.uimm());
        return;
    case 27:
        ppc::xori(cpu, instruction.ra(), instruction.rs(),
                  instruction.uimm() << 16);
        return;
    case 28:
        ppc::andi_dot(cpu, instruction.ra(), instruction.rs(),
                      instruction.uimm());
        return;
    case 29:
        ppc::andi_dot(cpu, instruction.ra(), instruction.rs(),
                      instruction.uimm() << 16);
        return;
    case 31:
        switch (xo) {
        case 0:
            ppc::cmp(cpu, instruction.bf(), instruction.ra(),
                     instruction.rb());
            return;
        case 4:
            ppc::tw(cpu, instruction.rt(), instruction.ra(), instruction.rb());
            return;
        case 19:
            ppc::mfcr(cpu, instruction.rt());
            return;
        case 23:
            ppc::lwzx(cpu, image_.memory, instruction.rt(), instruction.ra(),
                      instruction.rb());
            return;
        case 87:
            ppc::lbzx(cpu, image_.memory, instruction.rt(), instruction.ra(),
                      instruction.rb());
            return;
        case 343:
            ppc::lhax(cpu, image_.memory, instruction.rt(), instruction.ra(),
                      instruction.rb());
            return;
        case 279:
            ppc::lhzx(cpu, image_.memory, instruction.rt(), instruction.ra(),
                      instruction.rb());
            return;
        case 311:
            ppc::lhzux(cpu, image_.memory, instruction.rt(), instruction.ra(),
                       instruction.rb());
            return;
        case 535:
            ppc::lfsx(cpu, image_.memory, instruction.rt(), instruction.ra(),
                      instruction.rb());
            return;
        case 567:
            ppc::lfsux(cpu, image_.memory, instruction.rt(), instruction.ra(),
                       instruction.rb());
            return;
        case 55:
            ppc::lwzux(cpu, image_.memory, instruction.rt(), instruction.ra(),
                       instruction.rb());
            return;
        case 20:
            ppc::lwarx(cpu, image_.memory, instruction.rt(), instruction.ra(),
                       instruction.rb());
            return;
        case 119:
            ppc::lbzux(cpu, image_.memory, instruction.rt(), instruction.ra(),
                       instruction.rb());
            return;
        case 375:
            ppc::lhaux(cpu, image_.memory, instruction.rt(), instruction.ra(),
                       instruction.rb());
            return;
        case 534:
            ppc::lwbrx(cpu, image_.memory, instruction.rt(), instruction.ra(),
                       instruction.rb());
            return;
        case 183:
            ppc::stwux(cpu, image_.memory, instruction.rs(), instruction.ra(),
                       instruction.rb());
            return;
        case 695:
            ppc::stfsux(cpu, image_.memory, instruction.rs(), instruction.ra(),
                        instruction.rb());
            return;
        case 150:
            ppc::stwcx(cpu, image_.memory, instruction.rs(), instruction.ra(),
                       instruction.rb());
            return;
        case 247:
            ppc::stbux(cpu, image_.memory, instruction.rs(), instruction.ra(),
                       instruction.rb());
            return;
        case 439:
            ppc::sthux(cpu, image_.memory, instruction.rs(), instruction.ra(),
                       instruction.rb());
            return;
        case 662:
            ppc::stwbrx(cpu, image_.memory, instruction.rs(), instruction.ra(),
                        instruction.rb());
            return;
        case 1014:
            ppc::dcbz(cpu, image_.memory, instruction.ra(), instruction.rb());
            return;
        case 54:
            ppc::dcbst(cpu);
            return;
        case 86:
            ppc::dcbf(cpu);
            return;
        case 32:
            ppc::cmpl(cpu, instruction.bf(), instruction.ra(),
                      instruction.rb());
            return;
        case 8:
            ppc::subfc(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb(), instruction.rc());
            return;
        case 24:
            ppc::slw(cpu, instruction.ra(), instruction.rs(),
                     instruction.rb(), instruction.rc());
            return;
        case 26:
            ppc::cntlzw(cpu, instruction.ra(), instruction.rs(),
                        instruction.rc());
            return;
        case 28:
            ppc::and_(cpu, instruction.ra(), instruction.rs(),
                      instruction.rb(), instruction.rc());
            return;
        case 40:
            ppc::subf(cpu, instruction.rt(), instruction.ra(),
                      instruction.rb(), instruction.rc());
            return;
        case 60:
            ppc::andc(cpu, instruction.ra(), instruction.rs(),
                      instruction.rb(), instruction.rc());
            return;
        case 104:
            ppc::neg(cpu, instruction.rt(), instruction.ra(),
                     instruction.rc());
            return;
        case 124:
            ppc::nor_(cpu, instruction.ra(), instruction.rs(),
                      instruction.rb(), instruction.rc());
            return;
        case 136:
            ppc::subfe(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb(), instruction.rc());
            return;
        case 151:
            ppc::stwx(cpu, image_.memory, instruction.rs(), instruction.ra(),
                      instruction.rb());
            return;
        case 215:
            ppc::stbx(cpu, image_.memory, instruction.rs(), instruction.ra(),
                      instruction.rb());
            return;
        case 10:
            ppc::addc(cpu, instruction.rt(), instruction.ra(),
                      instruction.rb(), instruction.rc());
            return;
        case 200:
        case 712:
            ppc::subfze(cpu, instruction.rt(), instruction.ra(), xo == 712,
                        instruction.rc());
            return;
        case 202:
        case 714:
            ppc::addze(cpu, instruction.rt(), instruction.ra(), xo == 714,
                       instruction.rc());
            return;
        case 138:
            ppc::adde(cpu, instruction.rt(), instruction.ra(),
                      instruction.rb(), false, instruction.rc());
            return;
        case 412:
            ppc::orc(cpu, instruction.ra(), instruction.rs(),
                     instruction.rb(), instruction.rc());
            return;
        case 407:
            ppc::sthx(cpu, image_.memory, instruction.rs(), instruction.ra(),
                      instruction.rb());
            return;
        case 11:
            ppc::mulhwu(cpu, instruction.rt(), instruction.ra(),
                        instruction.rb(), instruction.rc());
            return;
        case 75:
            ppc::mulhw(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb(), instruction.rc());
            return;
        case 235:
            ppc::mullw(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb(), instruction.rc());
            return;
        case 266:
            ppc::add(cpu, instruction.rt(), instruction.ra(),
                     instruction.rb(), instruction.rc());
            return;
        case 339:
            ppc::mfspr(cpu, instruction.rt(), instruction.spr());
            return;
        case 316:
            ppc::xor_(cpu, instruction.ra(), instruction.rs(),
                      instruction.rb(), instruction.rc());
            return;
        case 444:
            ppc::or_(cpu, instruction.ra(), instruction.rs(),
                     instruction.rb(), instruction.rc());
            return;
        case 459:
            ppc::divwu(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb(), instruction.rc());
            return;
        case 491:
        case 1003:
            ppc::divw(cpu, instruction.rt(), instruction.ra(),
                      instruction.rb(), xo == 1003, instruction.rc());
            return;
        case 467:
            ppc::mtspr(cpu, instruction.rs(), instruction.spr());
            return;
        case 536:
            ppc::srw(cpu, instruction.ra(), instruction.rs(),
                     instruction.rb(), instruction.rc());
            return;
        case 597:
            ppc::lswi(cpu, image_.memory, instruction.rt(), instruction.ra(),
                      instruction.rb());
            return;
        case 725:
            ppc::stswi(cpu, image_.memory, instruction.rs(), instruction.ra(),
                       instruction.rb());
            return;
        case 824:
            ppc::srawi(cpu, instruction.ra(), instruction.rs(),
                       instruction.sh(), instruction.rc());
            return;
        case 792:
            ppc::sraw(cpu, instruction.ra(), instruction.rs(),
                      instruction.rb(), instruction.rc());
            return;
        case 922:
            ppc::extsh(cpu, instruction.ra(), instruction.rs(),
                       instruction.rc());
            return;
        case 954:
            ppc::extsb(cpu, instruction.ra(), instruction.rs(),
                       instruction.rc());
            return;
        case 983:
            ppc::stfiwx(cpu, image_.memory, instruction.rs(),
                        instruction.ra(), instruction.rb());
            return;
        case 663:
            ppc::stfsx(cpu, image_.memory, instruction.rs(),
                       instruction.ra(), instruction.rb());
            return;
        }
        break;
    case 32:
        ppc::lwz(cpu, image_.memory, instruction.rt(), instruction.ra(),
                 instruction.simm());
        return;
    case 33:
        ppc::lwzu(cpu, image_.memory, instruction.rt(), instruction.ra(),
                  instruction.simm());
        return;
    case 34:
        ppc::lbz(cpu, image_.memory, instruction.rt(), instruction.ra(),
                 instruction.simm());
        return;
    case 35:
        ppc::lbzu(cpu, image_.memory, instruction.rt(), instruction.ra(),
                  instruction.simm());
        return;
    case 36:
        ppc::stw(cpu, image_.memory, instruction.rs(), instruction.ra(),
                 instruction.simm());
        return;
    case 37:
        ppc::stwu(cpu, image_.memory, instruction.rs(), instruction.ra(),
                  instruction.simm());
        return;
    case 38:
        ppc::stb(cpu, image_.memory, instruction.rs(), instruction.ra(),
                 instruction.simm());
        return;
    case 39:
        ppc::stbu(cpu, image_.memory, instruction.rs(), instruction.ra(),
                  instruction.simm());
        return;
    case 40:
        ppc::lhz(cpu, image_.memory, instruction.rt(), instruction.ra(),
                 instruction.simm());
        return;
    case 41:
        ppc::lhzu(cpu, image_.memory, instruction.rt(), instruction.ra(),
                  instruction.simm());
        return;
    case 42:
        ppc::lha(cpu, image_.memory, instruction.rt(), instruction.ra(),
                 instruction.simm());
        return;
    case 43:
        ppc::lhau(cpu, image_.memory, instruction.rt(), instruction.ra(),
                  instruction.simm());
        return;
    case 44:
        ppc::sth(cpu, image_.memory, instruction.rs(), instruction.ra(),
                 instruction.simm());
        return;
    case 45:
        ppc::sthu(cpu, image_.memory, instruction.rs(), instruction.ra(),
                  instruction.simm());
        return;
    case 46:
        ppc::lmw(cpu, image_.memory, instruction.rt(), instruction.ra(),
                 instruction.simm());
        return;
    case 47:
        ppc::stmw(cpu, image_.memory, instruction.rs(), instruction.ra(),
                  instruction.simm());
        return;
    case 48:
        ppc::lfs(cpu, image_.memory, instruction.rt(), instruction.ra(),
                 instruction.simm());
        return;
    case 49:
        ppc::lfsu(cpu, image_.memory, instruction.rt(), instruction.ra(),
                  instruction.simm());
        return;
    case 50:
        ppc::lfd(cpu, image_.memory, instruction.rt(), instruction.ra(),
                 instruction.simm());
        return;
    case 52:
        ppc::stfs(cpu, image_.memory, instruction.rs(), instruction.ra(),
                  instruction.simm());
        return;
    case 53:
        ppc::stfsu(cpu, image_.memory, instruction.rs(), instruction.ra(),
                   instruction.simm());
        return;
    case 54:
        ppc::stfd(cpu, image_.memory, instruction.rs(), instruction.ra(),
                  instruction.simm());
        return;
    case 55:
        ppc::stfdu(cpu, image_.memory, instruction.rs(), instruction.ra(),
                   instruction.simm());
        return;
    case 56:
        ppc::psq_l(cpu, image_.memory, instruction.rt(), instruction.ra(),
                   instruction.ps_w(), instruction.ps_i(),
                   instruction.ps_displacement());
        return;
    case 60:
        ppc::psq_st(cpu, image_.memory, instruction.rs(), instruction.ra(),
                    instruction.ps_w(), instruction.ps_i(),
                    instruction.ps_displacement());
        return;
    case 57:
        ppc::psq_lu(cpu, image_.memory, instruction.rt(), instruction.ra(),
                    instruction.ps_w(), instruction.ps_i(),
                    instruction.ps_displacement());
        return;
    case 61:
        ppc::psq_stu(cpu, image_.memory, instruction.rs(), instruction.ra(),
                     instruction.ps_w(), instruction.ps_i(),
                     instruction.ps_displacement());
        return;
    case 59:
        if (xo == 24) {
            ppc::fres(cpu, instruction.rt(), instruction.rb());
        } else if (instruction.xo5() == 18) {
            ppc::fdivs(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb());
        } else if (instruction.xo5() == 25) {
            ppc::fmuls(cpu, instruction.rt(), instruction.ra(),
                       instruction.frc());
        } else if (instruction.xo5() == 29) {
            ppc::fmadds(cpu, instruction.rt(), instruction.ra(),
                        instruction.rb(), instruction.frc());
        } else if (instruction.xo5() == 30) {
            ppc::fnmsubs(cpu, instruction.rt(), instruction.ra(),
                         instruction.rb(), instruction.frc());
        } else if (instruction.xo5() == 28) {
            ppc::fmsubs(cpu, instruction.rt(), instruction.ra(),
                        instruction.rb(), instruction.frc());
        } else if (instruction.xo5() == 21) {
            ppc::fadds(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb());
        } else {
            ppc::fsubs(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb());
        }
        return;
    case 63:
        if (instruction.xo5() == 18) {
            ppc::fdiv(cpu, instruction.rt(), instruction.ra(),
                      instruction.rb());
        } else if (instruction.xo5() == 20) {
            ppc::fsub(cpu, instruction.rt(), instruction.ra(),
                      instruction.rb());
        } else if (instruction.xo5() == 21) {
            ppc::fadd(cpu, instruction.rt(), instruction.ra(),
                      instruction.rb());
        } else if (instruction.xo5() == 25) {
            ppc::fmul(cpu, instruction.rt(), instruction.ra(),
                      instruction.frc());
        } else if (instruction.xo5() == 28) {
            ppc::fmsub(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb(), instruction.frc());
        } else if (instruction.xo5() == 29) {
            ppc::fmadd(cpu, instruction.rt(), instruction.ra(),
                       instruction.rb(), instruction.frc());
        } else if (instruction.xo5() == 30) {
            ppc::fnmsub(cpu, instruction.rt(), instruction.ra(),
                        instruction.rb(), instruction.frc());
        } else if (instruction.xo5() == 23) {
            ppc::fsel(cpu, instruction.rt(), instruction.ra(),
                      instruction.frc(), instruction.rb());
        } else if (xo == 0) {
            ppc::fcmpu(cpu, instruction.bf(), instruction.ra(),
                       instruction.rb());
        } else if (xo == 12) {
            ppc::frsp(cpu, instruction.rt(), instruction.rb());
        } else if (xo == 14) {
            ppc::fctiw(cpu, instruction.rt(), instruction.rb());
        } else if (xo == 15) {
            ppc::fctiwz(cpu, instruction.rt(), instruction.rb());
        } else if (xo == 40) {
            ppc::fneg(cpu, instruction.rt(), instruction.rb());
        } else if (xo == 264) {
            ppc::fabs(cpu, instruction.rt(), instruction.rb());
        } else if (xo == 136) {
            ppc::fnabs(cpu, instruction.rt(), instruction.rb());
        } else if (xo == 26) {
            ppc::frsqrte(cpu, instruction.rt(), instruction.rb());
        } else {
            ppc::fmr(cpu, instruction.rt(), instruction.rb());
        }
        return;
    }
}

ExecutionStop Executor::run(CPUContext& cpu, uint64_t max_instructions) {
    const auto slice = run_slice(cpu, max_instructions);
    if (slice.terminal) {
        return *slice.terminal;
    }
    auto stop = make_stop(
        StopCategory::instruction_budget,
        slice.category == SliceCategory::quantum
            ? "instruction budget exhausted"
            : "execution rescheduled without scheduler",
        cpu);
    snapshot_trace(stop, 0);
    return stop;
}

ExecutionSlice Executor::run_slice(
    CPUContext& cpu, uint64_t absolute_instruction_endpoint) {
    return run_slice(cpu, absolute_instruction_endpoint, 0);
}

ExecutionSlice Executor::run_slice(
    CPUContext& cpu, uint64_t absolute_instruction_endpoint,
    uint32_t active_thread) {
    static std::unordered_map<uint32_t, uint64_t> opening_profile;
    const bool profile_opening =
        std::getenv("NWIIU_OPEN_PROFILE") != nullptr;
    const auto terminal = [this, active_thread,
                           profile_opening](ExecutionStop stop) {
        snapshot_trace(stop, active_thread);
        if (profile_opening) {
            std::vector<std::pair<uint64_t, uint32_t>> profile;
            profile.reserve(opening_profile.size());
            for (const auto& [pc, count] : opening_profile) {
                profile.emplace_back(count, pc);
            }
            std::ranges::sort(profile, std::greater{});
            std::fprintf(stderr, "OPEN PROFILE\n");
            for (size_t i = 0; i < std::min<size_t>(64, profile.size()); ++i) {
                std::fprintf(stderr, " %08X %llu\n", profile[i].second,
                             static_cast<unsigned long long>(profile[i].first));
            }
        }
        return ExecutionSlice{SliceCategory::terminal, std::move(stop)};
    };
    while (true) {
        if (!cpu.running) {
            return terminal(make_stop(StopCategory::guest_exit,
                                      "guest requested exit", cpu));
        }
        if (cpu.instruction_count >= absolute_instruction_endpoint) {
            return {SliceCategory::quantum, std::nullopt};
        }
        static std::unordered_map<uint32_t, uint64_t> logo_phase_calls;
        if (std::getenv("NWIIU_OPEN_TRACE") != nullptr &&
            (cpu.pc == 0x025AC1D4 || cpu.pc == 0x025AC3B8 ||
             cpu.pc == 0x025AC494 || cpu.pc == 0x025AC5E0)) {
            const uint64_t calls = ++logo_phase_calls[cpu.pc];
            if (calls == 1 || calls % 64 == 0) {
                std::fprintf(stderr,
                             "LOGO-PHASE pc=%08X call=%llu object=%08X"
                             " phase=%08X\n",
                             cpu.pc, static_cast<unsigned long long>(calls),
                             cpu.gpr[3],
                             image_.memory.read32(cpu.gpr[3] + 0x1C8,
                                                  cpu.pc));
            }
        }
        static uint64_t logo_phase_returns = 0;
        if (std::getenv("NWIIU_OPEN_TRACE") != nullptr &&
            cpu.pc == 0x02019FAC) {
            const uint64_t calls = logo_phase_returns++;
            if (calls < 10 || calls % 4096 == 0) {
                std::fprintf(stderr,
                             "LOGO-RETURN n=%llu value=%u request=%08X"
                             " table=%08X index=%u\n",
                             static_cast<unsigned long long>(calls),
                             cpu.gpr[3], cpu.gpr[31],
                             image_.memory.read32(cpu.gpr[31], cpu.pc),
                             image_.memory.read32(cpu.gpr[31] + 4, cpu.pc));
            }
        }

        if (profile_opening && cpu.current_instruction &&
            (*cpu.current_instruction & 0xFC0007FEu) == 0x4C000420u &&
            cpu.pc >= 0x02000000 && cpu.pc < 0x02900000) {
            if (++opening_profile[cpu.pc] == 64) {
                const size_t previous =
                    (cpu.history_cursor + cpu.pc_history.size() - 1) %
                    cpu.pc_history.size();
                std::fprintf(stderr, "INDIRECT %08X from=%08X\n", cpu.pc,
                             cpu.pc_history[previous]);
            }
        }
        RegisteredNative* native = nullptr;
        if ((cpu.pc & 3) == 0) {
            const auto& page = native_pages_[cpu.pc >> 16];
            if (page != nullptr) {
                auto& candidate = (*page)[(cpu.pc & 0xFFFF) >> 2];
                if (candidate.instruction_count != 0) {
                    native = &candidate;
                }
            }
        } else if (const auto found = unaligned_native_thunks_.find(cpu.pc);
                   found != unaligned_native_thunks_.end()) {
            native = &found->second;
        }
        if (native != nullptr &&
            native->instruction_count <=
                absolute_instruction_endpoint - cpu.instruction_count) {
            ++native_dispatch_count_;
            cpu.current_instruction.reset();
            cpu.native_instruction_endpoint = absolute_instruction_endpoint;
            cpu.native_executor = this;
            try {
                native->thunk(cpu, image_.memory);
            } catch (const GuestFault& fault) {
                return terminal(with_faulting_word(
                    make_guest_fault_stop(fault, cpu)));
            }
            continue;
        }
        if (native != nullptr) {
            ++native_fallback_count_;
        }

        const auto import = image_.imports.find(cpu.pc);
        if (import != image_.imports.end()) {
            const auto handler = hle_handlers_.find(cpu.pc);
            if (handler == hle_handlers_.end()) {
                auto stop = make_stop(StopCategory::missing_hle,
                                      "unimplemented Cafe import", cpu);
                stop.module = import->second.module;
                stop.symbol = import->second.symbol;
                std::copy_n(cpu.gpr.begin() + 3, stop.argument_gprs.size(),
                            stop.argument_gprs.begin());
                return terminal(std::move(stop));
            }
            cpu.current_instruction.reset();
            const size_t trace_slot =
                trace_enter(cpu.pc, active_thread, import->second);
            HleAction action;
            try {
                action = handler->second(cpu, image_.memory);
            } catch (const GuestFault& fault) {
                return terminal(with_faulting_word(
                    make_guest_fault_stop(fault, cpu)));
            }
            if (!cpu.running || action == HleAction::exit) {
                cpu.running = false;
                return terminal(make_stop(StopCategory::guest_exit,
                                          "guest requested exit", cpu));
            }
            if (action == HleAction::reschedule) {
                return {SliceCategory::reschedule, std::nullopt};
            }
            if (trace_slot < kHleTraceCapacity) {
                hle_trace_[trace_slot].returned = true;
            }
            cpu.pc = cpu.lr;
            continue;
        }


        try {
            step(cpu);
        } catch (const GuestFault& fault) {
            return terminal(with_faulting_word(
                make_guest_fault_stop(fault, cpu)));
        }
    }
}
} // namespace nwii::runtime
