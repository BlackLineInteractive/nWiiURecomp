#include "runtime/cafe_olv.h"

#include "runtime/cafe_runtime.h"
#include "runtime/execution_image.h"
#include "runtime/memory.h"

namespace nwii::runtime {
namespace {
constexpr uint32_t kDownloadedPostDataSize = 0xC208;
constexpr uint32_t kDownloadedDataBaseSize = 0xC008;
constexpr uint32_t kDownloadedTopicDataSize = 0x1000;
constexpr uint32_t kDownloadPostDataListParamSize = 0x1000;
}

CafeOlv::CafeOlv(ExecutionImage& image) : memory_(image.memory) {}

void CafeOlv::register_handlers(CafeRuntime& runtime) {
    runtime.register_handler(
        "nn_olv", "__ct__Q3_2nn3olv15InitializeParamFv",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t self = cpu.gpr[3];
            memory_.validate(self, 0x40, cpu.pc, MemoryAccess::write);
            for (uint32_t offset = 0; offset < 0x40; offset += 4) {
                memory_.write32(self + offset, 0, cpu.pc);
            }
            memory_.write32(self + 4, 0x1B7F, cpu.pc);
            cpu.gpr[3] = self;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_olv", "SetWork__Q3_2nn3olv15InitializeParamFPUcUi",
        [this](CPUContext& cpu, GuestMemory&) {
            memory_.write32(cpu.gpr[3] + 8, cpu.gpr[4], cpu.pc);
            memory_.write32(cpu.gpr[3] + 0xC, cpu.gpr[5], cpu.pc);
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_olv",
        "Initialize__Q2_2nn3olvFPCQ3_2nn3olv15InitializeParam",
        [this](CPUContext& cpu, GuestMemory&) {
            initialized_ = true;
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_olv", "IsInitialized__Q2_2nn3olvFv",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = initialized_;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_olv", "Finalize__Q2_2nn3olvFv",
        [this](CPUContext& cpu, GuestMemory&) {
            initialized_ = false;
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_olv", "SetReportTypes__Q3_2nn3olv6ReportFUi",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_olv", "__ct__Q3_2nn3olv18DownloadedPostDataFv",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto self = cpu.gpr[3];
            memory_.validate(self, kDownloadedPostDataSize, cpu.pc,
                             MemoryAccess::write);
            for (uint32_t offset = 0; offset < kDownloadedDataBaseSize;
                 offset += sizeof(uint32_t)) {
                memory_.write32(self + offset, 0, cpu.pc);
            }
            memory_.write32(self + 0xC000, kDownloadedPostDataVtable, cpu.pc);
            memory_.write32(self + 0xC008, 0, cpu.pc);
            memory_.write32(self + 0xC00C, 0, cpu.pc);
            memory_.write32(self + 0xC010, 0, cpu.pc);
            cpu.gpr[3] = self;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_olv", "__ct__Q3_2nn3olv19DownloadedTopicDataFv",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto self = cpu.gpr[3];
            memory_.validate(self, kDownloadedTopicDataSize, cpu.pc,
                             MemoryAccess::write);
            memory_.write32(self, 0, cpu.pc);
            memory_.write32(self + 4, 0, cpu.pc);
            cpu.gpr[3] = self;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "nn_olv", "__ct__Q3_2nn3olv25DownloadPostDataListParamFv",
        [this](CPUContext& cpu, GuestMemory&) {
            const auto self = cpu.gpr[3];
            memory_.validate(self, kDownloadPostDataListParamSize, cpu.pc,
                             MemoryAccess::write);
            for (uint32_t offset = 0; offset < kDownloadPostDataListParamSize;
                 offset += sizeof(uint32_t)) {
                memory_.write32(self + offset, 0, cpu.pc);
            }
            cpu.gpr[3] = self;
            return HleAction::return_to_lr;
        });
}
} // namespace nwii::runtime
