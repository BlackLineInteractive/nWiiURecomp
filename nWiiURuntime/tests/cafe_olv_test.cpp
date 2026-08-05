#include "runtime/cafe_olv.h"
#include "runtime/cafe_runtime.h"
#include "runtime/executor.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <type_traits>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::CafeOlv;
using nwii::runtime::CafeRuntime;
using nwii::runtime::ExecutionImage;
using nwii::runtime::Executor;
using nwii::runtime::MemoryAccess;
using nwii::runtime::StopCategory;

static_assert(!std::is_copy_constructible_v<CafeOlv>);
static_assert(!std::is_move_constructible_v<CafeOlv>);
static_assert(!std::is_copy_assignable_v<CafeOlv>);
static_assert(!std::is_move_assignable_v<CafeOlv>);

constexpr uint32_t kReturn = 0x02000000;
constexpr uint32_t kImport = 0xC0004F18;
constexpr uint32_t kData = 0x10202000;
constexpr uint32_t kSelf = kData + 0xCA0;
constexpr uint32_t kObjectSize = 0xC208;
constexpr uint32_t kBaseSize = 0xC008;

void test_downloaded_post_data_constructor() {
    ExecutionImage image;
    constexpr std::array<uint8_t, 4> branch{0x48, 0, 0, 0};
    image.memory.map(kReturn, branch.size(), {true, false, true}, branch);
    image.memory.map(kData, 0x10000, {true, true, false});
    image.imports.emplace(
        kImport, nwii::runtime::ImportTarget{
                     "nn_olv", "__ct__Q3_2nn3olv18DownloadedPostDataFv"});

    CafeOlv olv(image);
    CafeRuntime runtime(image);
    Executor executor(image);
    olv.register_handlers(runtime);
    runtime.register_imports(executor);

    for (uint32_t offset = 0; offset < kObjectSize; ++offset) {
        image.memory.write8(kSelf + offset, 0xA5, 0);
    }

    CPUContext cpu;
    cpu.pc = kImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSelf;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "DownloadedPostData constructor is bound and returns");
    test::require(cpu.gpr[3] == kSelf, "constructor returns self");
    for (uint32_t offset = 0; offset < 0xC000; ++offset) {
        test::require(image.memory.read8(kSelf + offset, 0) == 0,
                      "base payload is cleared");
    }
    test::require(image.memory.read32(kSelf + 0xC000, 0) ==
                      CafeOlv::kDownloadedPostDataVtable,
                  "constructor installs stable guest vtable identity");
    test::require(image.memory.read32(kSelf + 0xC004, 0) == 0,
                  "base tail is cleared");
    test::require(image.memory.read32(kSelf + 0xC008, 0) == 0 &&
                      image.memory.read32(kSelf + 0xC00C, 0) == 0 &&
                      image.memory.read32(kSelf + 0xC010, 0) == 0,
                  "derived counters are zeroed");
    for (uint32_t offset = 0xC014; offset < kObjectSize; ++offset) {
        test::require(image.memory.read8(kSelf + offset, 0) == 0xA5,
                      "uninitialized derived tail is preserved");
    }
}

void test_constructor_validates_full_writable_range_before_mutation() {
    ExecutionImage image;
    constexpr std::array<uint8_t, 4> branch{0x48, 0, 0, 0};
    image.memory.map(kReturn, branch.size(), {true, false, true}, branch);
    image.memory.map(kData, 0x8000, {true, true, false});
    image.memory.map(kData + 0x8000, 0x8000, {true, false, false});
    image.imports.emplace(
        kImport, nwii::runtime::ImportTarget{
                     "nn_olv", "__ct__Q3_2nn3olv18DownloadedPostDataFv"});

    CafeOlv olv(image);
    CafeRuntime runtime(image);
    Executor executor(image);
    olv.register_handlers(runtime);
    runtime.register_imports(executor);
    image.memory.write8(kSelf, 0xA5, 0);

    CPUContext cpu;
    cpu.pc = kImport;
    cpu.lr = kReturn;
    cpu.gpr[3] = kSelf;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::guest_fault,
                  "constructor rejects a partially read-only object");
    test::require(stop.fault_access == MemoryAccess::write &&
                      stop.fault_address == kSelf &&
                      stop.fault_width == kObjectSize,
                  "fault describes the complete writable object range");
    test::require(image.memory.read8(kSelf, 0) == 0xA5,
                  "validation faults before constructor mutation");
}

void test_downloaded_topic_data_constructor() {
    constexpr uint32_t import = 0xC0004F20;
    constexpr uint32_t self = kData + 0x3000;
    constexpr uint32_t size = 0x1000;
    ExecutionImage image;
    constexpr std::array<uint8_t, 4> branch{0x48, 0, 0, 0};
    image.memory.map(kReturn, branch.size(), {true, false, true}, branch);
    image.memory.map(kData, 0x10000, {true, true, false});
    image.imports.emplace(
        import, nwii::runtime::ImportTarget{
                    "nn_olv", "__ct__Q3_2nn3olv19DownloadedTopicDataFv"});

    CafeOlv olv(image);
    CafeRuntime runtime(image);
    Executor executor(image);
    olv.register_handlers(runtime);
    runtime.register_imports(executor);
    for (uint32_t offset = 0; offset < size; ++offset) {
        image.memory.write8(self + offset, 0xA5, 0);
    }

    CPUContext cpu;
    cpu.pc = import;
    cpu.lr = kReturn;
    cpu.gpr[3] = self;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "DownloadedTopicData constructor is bound and returns");
    test::require(cpu.gpr[3] == self, "topic constructor returns self");
    test::require(image.memory.read32(self, 0) == 0 &&
                      image.memory.read32(self + 4, 0) == 0,
                  "topic constructor clears its two proven words");
    for (uint32_t offset = 8; offset < size; ++offset) {
        test::require(image.memory.read8(self + offset, 0) == 0xA5,
                      "topic constructor preserves its uninitialized tail");
    }
}

void test_download_post_data_list_param_constructor() {
    constexpr uint32_t import = 0xC0004F50;
    constexpr uint32_t self = kData + 0x5000;
    constexpr uint32_t size = 0x1000;
    ExecutionImage image;
    constexpr std::array<uint8_t, 4> branch{0x48, 0, 0, 0};
    image.memory.map(kReturn, branch.size(), {true, false, true}, branch);
    image.memory.map(kData, 0x10000, {true, true, false});
    image.imports.emplace(
        import, nwii::runtime::ImportTarget{
                    "nn_olv", "__ct__Q3_2nn3olv25DownloadPostDataListParamFv"});

    CafeOlv olv(image);
    CafeRuntime runtime(image);
    Executor executor(image);
    olv.register_handlers(runtime);
    runtime.register_imports(executor);
    for (uint32_t offset = 0; offset < size; ++offset) {
        image.memory.write8(self + offset, 0xA5, 0);
    }

    CPUContext cpu;
    cpu.pc = import;
    cpu.lr = kReturn;
    cpu.gpr[3] = self;
    const auto stop = executor.run(cpu, 1);

    test::require(stop.category == StopCategory::instruction_budget,
                  "DownloadPostDataListParam constructor is bound and returns");
    test::require(cpu.gpr[3] == self, "parameter constructor returns self");
    for (uint32_t offset = 0; offset < size; ++offset) {
        test::require(image.memory.read8(self + offset, 0) == 0,
                      "parameter constructor clears its complete object");
    }
}

} // namespace

int main() {
    test_downloaded_post_data_constructor();
    test_constructor_validates_full_writable_range_before_mutation();
    test_downloaded_topic_data_constructor();
    test_download_post_data_list_param_constructor();
    return 0;
}
