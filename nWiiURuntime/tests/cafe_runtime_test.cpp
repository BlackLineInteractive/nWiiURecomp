#include "runtime/cafe_runtime.h"
#include "runtime/executor.h"
#include "test_support.h"

#include <cstdint>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::CafeRuntime;
using nwii::runtime::ExecutionImage;
using nwii::runtime::HleAction;
using nwii::runtime::Executor;
using nwii::runtime::GuestMemory;
using nwii::runtime::StopCategory;

constexpr uint32_t kReturn = 0x02000000;
constexpr uint32_t kData = 0x10000000;
constexpr uint32_t kCapturedA = 0xC0001000;
constexpr uint32_t kCapturedB = 0xC0001004;
constexpr uint32_t kModuleMismatch = 0xC0001008;
constexpr uint32_t kSymbolMismatch = 0xC000100C;
constexpr uint32_t kMissing = 0xC0001010;
constexpr uint32_t kFault = 0xC0001014;
constexpr uint32_t kExit = 0xC0001018;
constexpr uint32_t kAxInit = 0xC000101C;
constexpr uint32_t kAxSetDefaultMixerSelect = 0xC0001020;
constexpr uint32_t kAxSetDeviceLinearUpsampler = 0xC0001024;
constexpr uint32_t kAxGetDeviceFinalMixCallback = 0xC0001028;
constexpr uint32_t kVpadRead = 0xC000102C;
constexpr uint32_t kVpadCalibrateTouch = 0xC0001030;
constexpr uint32_t kAxUserBegin = 0xC0001034;
constexpr uint32_t kAxUserEnd = 0xC0001038;
constexpr uint32_t kAxUserIsProtected = 0xC000103C;

ExecutionImage make_image() {
    ExecutionImage image;
    image.memory.map(kData, 0x200, {true, true, false});
    image.memory.map(kReturn, 4, {true, false, true},
                     {reinterpret_cast<const uint8_t*>("\x48\0\0\0"), 4});
    image.imports.emplace(kCapturedA,
                          nwii::runtime::ImportTarget{"coreinit", "Captured"});
    image.imports.emplace(kCapturedB,
                          nwii::runtime::ImportTarget{"coreinit", "Captured"});
    image.imports.emplace(
        kModuleMismatch,
        nwii::runtime::ImportTarget{"other", "Captured"});
    image.imports.emplace(
        kSymbolMismatch,
        nwii::runtime::ImportTarget{"coreinit", "Other"});
    image.imports.emplace(kMissing,
                          nwii::runtime::ImportTarget{"coreinit", "Missing"});
    image.imports.emplace(kFault,
                          nwii::runtime::ImportTarget{"coreinit", "Fault"});
    image.imports.emplace(kExit,
                          nwii::runtime::ImportTarget{"coreinit", "Exit"});
    image.imports.emplace(kAxInit,
                          nwii::runtime::ImportTarget{"snd_core", "AXInit"});
    image.imports.emplace(
        kAxSetDefaultMixerSelect,
        nwii::runtime::ImportTarget{"snd_core", "AXSetDefaultMixerSelect"});
    image.imports.emplace(
        kAxSetDeviceLinearUpsampler,
        nwii::runtime::ImportTarget{"snd_core", "AXSetDeviceLinearUpsampler"});
    image.imports.emplace(
        kAxGetDeviceFinalMixCallback,
        nwii::runtime::ImportTarget{"snd_core",
                                    "AXGetDeviceFinalMixCallback"});
    image.imports.emplace(kVpadRead,
                          nwii::runtime::ImportTarget{"vpad", "VPADRead"});
    image.imports.emplace(
        kVpadCalibrateTouch,
        nwii::runtime::ImportTarget{"vpad", "VPADGetTPCalibratedPoint"});
    image.imports.emplace(
        kAxUserBegin,
        nwii::runtime::ImportTarget{"snd_core", "AXUserBegin"});
    image.imports.emplace(
        kAxUserEnd,
        nwii::runtime::ImportTarget{"snd_core", "AXUserEnd"});
    image.imports.emplace(
        kAxUserIsProtected,
        nwii::runtime::ImportTarget{"snd_core", "AXUserIsProtected"});
    return image;
}

void test_exact_routing_and_captured_state() {
    auto image = make_image();
    CafeRuntime cafe(image);
    uint32_t calls{};
    cafe.register_handler("coreinit", "Captured",
                          [&](CPUContext& cpu, GuestMemory&) {
                              ++calls;
                              cpu.gpr[3] = 0x12345678;
                              return HleAction::return_to_lr;
                          });
    Executor executor(image);
    cafe.register_imports(executor);

    CPUContext cpu;
    cpu.pc = kCapturedA;
    cpu.lr = kReturn;
    auto stop = executor.run(cpu, 1);
    test::require(calls == 1 && cpu.gpr[3] == 0x12345678,
                  "captured stateful handler");
    test::require(cpu.pc == kReturn &&
                      stop.category == StopCategory::instruction_budget &&
                      stop.instruction_count == 1 && stop.history_size == 1 &&
                      stop.history[0] == kReturn,
                  "HLE returns through LR without tracing import");

    cpu.pc = kCapturedB;
    cpu.lr = kReturn;
    stop = executor.run(cpu, 2);
    test::require(calls == 2 && cpu.pc == kReturn &&
                      stop.category == StopCategory::instruction_budget,
                  "same implementation binds every distinct import address");
}

void require_missing(Executor& executor, uint32_t address,
                     const char* module, const char* symbol,
                     const char* message) {
    CPUContext cpu;
    cpu.pc = address;
    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::missing_hle &&
                      stop.module == module && stop.symbol == symbol &&
                      stop.instruction_count == 0 && stop.history_size == 0,
                  message);
}

void test_exact_mismatches_and_missing_import() {
    auto image = make_image();
    CafeRuntime cafe(image);
    cafe.register_handler("coreinit", "Captured",
                          [](CPUContext&, GuestMemory&) {
                              return HleAction::return_to_lr;
                          });
    Executor executor(image);
    cafe.register_imports(executor);

    require_missing(executor, kModuleMismatch, "other", "Captured",
                    "module mismatch remains exact missing_hle");
    require_missing(executor, kSymbolMismatch, "coreinit", "Other",
                    "symbol mismatch remains exact missing_hle");
    require_missing(executor, kMissing, "coreinit", "Missing",
                    "unmatched import remains exact missing_hle");
}

void test_duplicate_registration_rejected() {
    auto image = make_image();
    CafeRuntime cafe(image);
    cafe.register_handler("coreinit", "Captured",
                          [](CPUContext&, GuestMemory&) {
                              return HleAction::return_to_lr;
                          });
    test::require_throws(
        [&] {
            cafe.register_handler("coreinit", "Captured",
                                  [](CPUContext&, GuestMemory&) {
                                      return HleAction::return_to_lr;
                                  });
        },
        "already registered", "duplicate implementation pair rejected");

    Executor executor(image);
    executor.register_hle(kCapturedA, [](CPUContext&, GuestMemory&) {
        return HleAction::return_to_lr;
    });
    test::require_throws(
        [&] {
            executor.register_hle(kCapturedA,
                                  [](CPUContext&, GuestMemory&) {
                                      return HleAction::return_to_lr;
                                  });
        },
        "already registered", "duplicate HLE address rejected");
}

void test_fault_and_exit_preserved() {
    auto image = make_image();
    CafeRuntime cafe(image);
    cafe.register_handler("coreinit", "Fault",
                          [](CPUContext& cpu, GuestMemory& memory) {
                              memory.read32(0x30000000, cpu.pc);
                              return HleAction::return_to_lr;
                          });
    cafe.register_handler("coreinit", "Exit",
                          [](CPUContext& cpu, GuestMemory&) {
                              cpu.gpr[3] = 0x55;
                              return HleAction::exit;
                          });
    Executor executor(image);
    cafe.register_imports(executor);

    CPUContext fault_cpu;
    fault_cpu.pc = kFault;
    fault_cpu.lr = kReturn;
    const auto fault = executor.run(fault_cpu, 1);
    test::require(fault.category == StopCategory::guest_fault &&
                      fault.pc == kFault && fault.fault_address == 0x30000000 &&
                      fault.instruction_count == 0 &&
                      fault_cpu.pc == kFault,
                  "handler GuestFault converts without LR return");

    CPUContext exit_cpu;
    exit_cpu.pc = kExit;
    exit_cpu.lr = kReturn;
    const auto exit = executor.run(exit_cpu, 1);
    test::require(exit.category == StopCategory::guest_exit &&
                      exit_cpu.gpr[3] == 0x55 && exit_cpu.pc == kExit &&
                      exit.instruction_count == 0,
                  "guest exit stops without LR return");
}
void test_ax_init_returns_to_guest() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);

    CPUContext cpu;
    cpu.pc = kAxInit;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0x12345678;
    auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturn && cpu.gpr[3] == 0x12345678,
                  "AXInit returns as a void audio initialization call");

    cpu.pc = kAxSetDefaultMixerSelect;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0;
    stop = executor.run(cpu, 2);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturn && cpu.gpr[3] == 0,
                  "AXSetDefaultMixerSelect returns AX_RESULT_SUCCESS");

    cpu.pc = kAxSetDeviceLinearUpsampler;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = 0;
    cpu.gpr[5] = 0;
    stop = executor.run(cpu, 3);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturn && cpu.gpr[3] == 0,
                  "AXSetDeviceLinearUpsampler returns AX_RESULT_SUCCESS");

    image.memory.write32(kData, 0xA5A5A5A5, 0);
    cpu.pc = kAxGetDeviceFinalMixCallback;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = kData;
    stop = executor.run(cpu, 4);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.gpr[3] == 0 && image.memory.read32(kData, 0) == 0,
                  "AXGetDeviceFinalMixCallback returns no muted callback");
}

void test_ax_user_protection_tracks_nesting() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    cpu.lr = kReturn;

    cpu.pc = kAxUserIsProtected;
    executor.run(cpu, 1);
    test::require(cpu.gpr[3] == 0, "AX starts unprotected");
    cpu.pc = kAxUserBegin;
    executor.run(cpu, 2);
    cpu.pc = kAxUserBegin;
    executor.run(cpu, 3);
    test::require(cpu.gpr[3] == 1, "AXUserBegin returns prior nesting");
    cpu.pc = kAxUserIsProtected;
    executor.run(cpu, 4);
    test::require(cpu.gpr[3] == 1, "AX reports nested protection");
    cpu.pc = kAxUserEnd;
    executor.run(cpu, 5);
    cpu.pc = kAxUserEnd;
    executor.run(cpu, 6);
    cpu.pc = kAxUserIsProtected;
    executor.run(cpu, 7);
    test::require(cpu.gpr[3] == 0, "AX protection clears after paired ends");
}

void test_vpad_read_reports_disconnected_controller() {
    auto image = make_image();
    image.memory.write32(kData, 0xDEADBEEF, 0);
    image.memory.write32(kData + 0xA8, 0xDEADBEEF, 0);
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kVpadRead;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = kData;
    cpu.gpr[5] = 16;
    cpu.gpr[6] = kData + 0x100;

    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturn && cpu.gpr[3] == 0 &&
                      image.memory.read32(kData, 0) == 0 &&
                      image.memory.read32(kData + 0xA8, 0) == 0 &&
                      image.memory.read32(kData + 0x100, 0) == 0xFFFFFFFE,
                  "VPADRead clears one sample and reports no controller");
}

void test_vpad_read_reports_host_button_transitions() {
    auto image = make_image();
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = kData;
    cpu.gpr[5] = 1;
    cpu.gpr[6] = kData + 0x100;

    cafe.set_vpad_buttons(1u << 15);
    cpu.pc = kVpadRead;
    auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.gpr[3] == 1 &&
                      image.memory.read32(kData, 0) == (1u << 15) &&
                      image.memory.read32(kData + 4, 0) == (1u << 15) &&
                      image.memory.read32(kData + 8, 0) == 0 &&
                      image.memory.read16(kData + 0x58, 0) == 3 &&
                      image.memory.read16(kData + 0x60, 0) == 3 &&
                      image.memory.read16(kData + 0x68, 0) == 3 &&
                      image.memory.read32(kData + 0x100, 0) == 0,
                  "VPADRead exposes a newly pressed host A button");

    cpu.pc = kVpadRead;
    cpu.gpr[3] = 0;
    stop = executor.run(cpu, 2);
    test::require(stop.category == StopCategory::instruction_budget &&
                      image.memory.read32(kData, 0) == (1u << 15) &&
                      image.memory.read32(kData + 4, 0) == 0,
                  "VPAD trigger is reported once");

    cafe.set_vpad_buttons(0);
    cpu.gpr[3] = 0;
    cpu.pc = kVpadRead;
    stop = executor.run(cpu, 3);
    test::require(stop.category == StopCategory::instruction_budget &&
                      image.memory.read32(kData, 0) == 0 &&
                      image.memory.read32(kData + 8, 0) == (1u << 15),
                  "VPADRead exposes a released host A button");
}

void test_vpad_touch_calibration_matches_drc_resolution() {
    auto image = make_image();
    image.memory.write16(kData + 0x80, 4095, 0);
    image.memory.write16(kData + 0x82, 0, 0);
    image.memory.write16(kData + 0x84, 1, 0);
    image.memory.write16(kData + 0x86, 0, 0);
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    cpu.pc = kVpadCalibrateTouch;
    cpu.lr = kReturn;
    cpu.gpr[3] = 0;
    cpu.gpr[4] = kData;
    cpu.gpr[5] = kData + 0x80;

    const auto stop = executor.run(cpu, 1);
    test::require(stop.category == StopCategory::instruction_budget &&
                      cpu.pc == kReturn &&
                      image.memory.read16(kData, 0) == 1279 &&
                      image.memory.read16(kData + 2, 0) == 720 &&
                      image.memory.read16(kData + 4, 0) == 1 &&
                      image.memory.read16(kData + 6, 0) == 0,
                  "VPAD touch calibration scales to 1280x720");
}

void test_final_mix_capture_interleaves_guest_stereo() {
    auto image = make_image();
    image.memory.map(0x0101D000, 0x3000, {true, true, false});
    CafeRuntime cafe(image);
    image.memory.write32(0x0101D100, 0xFFFF8000, 0);
    image.memory.write32(0x0101D100 + 144 * 4, 0x00007FFF, 0);

    cafe.capture_audio_frame(0x0101D000);
    std::array<int16_t, CafeRuntime::kAudioFrameSamples> samples{};
    test::require(
        cafe.pop_audio_frame(samples) && samples[0] == INT16_MIN &&
            samples[1] == INT16_MAX &&
            image.memory.read32(0x0101D100, 0) == 0 &&
            image.memory.read32(0x0101D100 + 144 * 4, 0) == 0 &&
            !cafe.pop_audio_frame(samples),
        "AX final mix capture interleaves stereo and clears guest buffers");
}

void test_ax_voice_mixes_pcm16_into_final_output() {
    constexpr uint32_t acquire = 0xC0001040;
    constexpr uint32_t set_offsets = 0xC0001044;
    constexpr uint32_t set_mix = 0xC0001048;
    constexpr uint32_t set_ve = 0xC000104C;
    constexpr uint32_t set_state = 0xC0001050;
    auto image = make_image();
    image.memory.map(0x01000000, 0x20000, {true, true, false});
    image.imports.emplace(
        acquire, nwii::runtime::ImportTarget{"snd_core", "AXAcquireVoiceEx"});
    image.imports.emplace(
        set_offsets,
        nwii::runtime::ImportTarget{"snd_core", "AXSetVoiceOffsets"});
    image.imports.emplace(
        set_mix,
        nwii::runtime::ImportTarget{"snd_core", "AXSetVoiceDeviceMix"});
    image.imports.emplace(set_ve,
                          nwii::runtime::ImportTarget{"snd_core",
                                                      "AXSetVoiceVe"});
    image.imports.emplace(
        set_state,
        nwii::runtime::ImportTarget{"snd_core", "AXSetVoiceState"});
    CafeRuntime cafe(image);
    Executor executor(image);
    cafe.register_imports(executor);
    CPUContext cpu;
    cpu.lr = kReturn;
    const auto call = [&](uint32_t function,
                          std::initializer_list<uint32_t> arguments) {
        std::fill(cpu.gpr.begin() + 3, cpu.gpr.end(), 0);
        std::copy(arguments.begin(), arguments.end(), cpu.gpr.begin() + 3);
        cpu.pc = function;
        const auto stop = executor.run(cpu, cpu.instruction_count + 1);
        test::require(stop.category == StopCategory::instruction_budget,
                      "AX voice call returns");
        return cpu.gpr[3];
    };

    image.memory.write16(kData, 1000, 0);
    image.memory.write16(kData + 2, static_cast<uint16_t>(-1000), 0);
    image.memory.write16(kData + 4, 2000, 0);
    image.memory.write16(kData + 6, static_cast<uint16_t>(-2000), 0);
    constexpr uint32_t offsets = kData + 0x100;
    image.memory.write16(offsets, 0x0A, 0);
    image.memory.write32(offsets + 8, 3, 0);
    image.memory.write32(offsets + 0x10, kData, 0);
    constexpr uint32_t mix = kData + 0x120;
    image.memory.write16(mix, 0x8000, 0);
    image.memory.write16(mix + 0x10, 0x8000, 0);
    constexpr uint32_t ve = kData + 0x190;
    image.memory.write16(ve, 0x8000, 0);

    const uint32_t voice = call(acquire, {31, 0, 0});
    test::require(voice != 0, "AXAcquireVoiceEx returns a voice");
    call(set_offsets, {voice, offsets});
    call(set_mix, {voice, 0, 0, mix});
    call(set_ve, {voice, ve});
    call(set_state, {voice, 1});
    cafe.prepare_audio_frame(0x0101D000);
    cafe.capture_audio_frame(0x0101D000);

    std::array<int16_t, CafeRuntime::kAudioFrameSamples> samples{};
    test::require(cafe.pop_audio_frame(samples) && samples[0] == 1000 &&
                      samples[1] == 1000 && samples[2] == -1000 &&
                      samples[3] == -1000,
                  "AX PCM16 voice is mixed to host stereo");
}

} // namespace

int main() {
    test_exact_routing_and_captured_state();
    test_exact_mismatches_and_missing_import();
    test_duplicate_registration_rejected();
    test_fault_and_exit_preserved();
    test_ax_init_returns_to_guest();
    test_ax_user_protection_tracks_nesting();
    test_vpad_read_reports_disconnected_controller();
    test_vpad_read_reports_host_button_transitions();
    test_vpad_touch_calibration_matches_drc_resolution();
    test_final_mix_capture_interleaves_guest_stereo();
    test_ax_voice_mixes_pcm16_into_final_output();
}
