#include "runtime/cafe_runtime.h"
#include "runtime/machine.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace nwii::runtime {
namespace {
constexpr std::array<uint32_t, 2> kAxFinalMixData{0x0101D000, 0x0101E000};
constexpr std::array<uint16_t, 2> kAxChannels{6, 4};
constexpr std::array<uint16_t, 2> kAxDevices{1, 2};
constexpr uint16_t kAxSamples = 144;
constexpr uint32_t kAxSampleStride = kAxSamples * sizeof(uint32_t);
constexpr uint32_t kAxSamplesOffset = 0x100;
constexpr uint32_t kAxVoiceBase = 0x01012000;
constexpr uint32_t kAxVoiceSize = 0x58;
}

CafeRuntime::CafeRuntime(ExecutionImage& image)
    : image_(image), proc_ui_(image), gx2_(image) {
    proc_ui_.register_handlers(*this);
    padscore_.register_handlers(*this);
    act_.register_handlers(*this);
    gx2_.register_handlers(*this);
    const auto init_audio = [this](CPUContext&, GuestMemory&) {
        ax_voices_.fill({});
        return HleAction::return_to_lr;
    };
    register_handler("snd_core", "AXInit", init_audio);
    register_handler("snd_core", "AXInitProfile", init_audio);
    register_handler(
        "snd_core", "AXQuit",
        [this](CPUContext&, GuestMemory&) {
            ax_voices_.fill({});
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXAcquireVoiceEx",
        [this](CPUContext& cpu, GuestMemory& memory) {
            for (uint32_t index = 0; index < ax_voices_.size(); ++index) {
                auto& state = ax_voices_[index];
                if (state.acquired) {
                    continue;
                }
                state = {};
                state.acquired = true;
                const uint32_t voice = kAxVoiceBase + index * kAxVoiceSize;
                for (uint32_t offset = 0; offset < kAxVoiceSize; ++offset) {
                    memory.write8(voice + offset, 0, cpu.pc);
                }
                memory.write32(voice, index, cpu.pc);
                memory.write32(voice + 0x1C, cpu.gpr[3], cpu.pc);
                memory.write32(voice + 0x24, cpu.gpr[5], cpu.pc);
                memory.write32(voice + 0x48, cpu.gpr[4], cpu.pc);
                cpu.gpr[3] = voice;
                return HleAction::return_to_lr;
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXFreeVoice",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            voice = {};
            memory.write32(cpu.gpr[3] + 4, 0, cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceOffsets",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            const uint32_t offsets = cpu.gpr[4];
            memory.validate_range(offsets, 0x14, cpu.pc, MemoryAccess::read);
            voice.format = memory.read16(offsets, cpu.pc);
            voice.looping = memory.read16(offsets + 2, cpu.pc) != 0;
            voice.loop_offset = memory.read32(offsets + 4, cpu.pc);
            voice.end_offset = memory.read32(offsets + 8, cpu.pc);
            voice.current_offset = memory.read32(offsets + 0xC, cpu.pc);
            voice.data = memory.read32(offsets + 0x10, cpu.pc);
            for (uint32_t offset = 0; offset < 0x14; offset += 2) {
                memory.write16(cpu.gpr[3] + 0x34 + offset,
                               memory.read16(offsets + offset, cpu.pc), cpu.pc);
            }
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXGetVoiceOffsets",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const auto& voice = ax_voice(cpu.gpr[3]);
            const uint32_t offsets = cpu.gpr[4];
            memory.write16(offsets, voice.format, cpu.pc);
            memory.write16(offsets + 2, voice.looping, cpu.pc);
            memory.write32(offsets + 4, voice.loop_offset, cpu.pc);
            memory.write32(offsets + 8, voice.end_offset, cpu.pc);
            memory.write32(offsets + 0xC, voice.current_offset, cpu.pc);
            memory.write32(offsets + 0x10, voice.data, cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXGetVoiceCurrentOffsetEx",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = ax_voice(cpu.gpr[3]).current_offset;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXGetVoiceLoopCount",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = ax_voice(cpu.gpr[3]).loop_count;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceEndOffsetEx",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            voice.end_offset = cpu.gpr[4];
            voice.data = cpu.gpr[5];
            memory.write32(cpu.gpr[3] + 0x3C, voice.end_offset, cpu.pc);
            memory.write32(cpu.gpr[3] + 0x44, voice.data, cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceLoopOffsetEx",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            voice.loop_offset = cpu.gpr[4];
            voice.data = cpu.gpr[5];
            memory.write32(cpu.gpr[3] + 0x38, voice.loop_offset, cpu.pc);
            memory.write32(cpu.gpr[3] + 0x44, voice.data, cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceLoop",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            voice.looping = cpu.gpr[4] != 0;
            memory.write16(cpu.gpr[3] + 0x36, voice.looping, cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceState",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            voice.playing = cpu.gpr[4] != 0;
            memory.write32(cpu.gpr[3] + 4, voice.playing, cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceType",
        [this](CPUContext& cpu, GuestMemory&) {
            ax_voice(cpu.gpr[3]).streaming = cpu.gpr[4] != 0;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoicePriority",
        [this](CPUContext& cpu, GuestMemory& memory) {
            ax_voice(cpu.gpr[3]);
            memory.write32(cpu.gpr[3] + 0x1C, cpu.gpr[4], cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceMixerSelect",
        [this](CPUContext& cpu, GuestMemory& memory) {
            ax_voice(cpu.gpr[3]);
            const uint32_t previous = memory.read32(cpu.gpr[3] + 0xC, cpu.pc);
            memory.write32(cpu.gpr[3] + 0xC, cpu.gpr[4], cpu.pc);
            cpu.gpr[3] = previous;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceDeviceMix",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            if (cpu.gpr[4] == 0 && cpu.gpr[5] == 0 && cpu.gpr[6] != 0) {
                voice.mix[0] = memory.read16(cpu.gpr[6], cpu.pc);
                voice.mix[1] = memory.read16(cpu.gpr[6] + 0x10, cpu.pc);
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceVe",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            voice.volume = memory.read16(cpu.gpr[4], cpu.pc);
            voice.volume_delta =
                static_cast<int16_t>(memory.read16(cpu.gpr[4] + 2, cpu.pc));
            memory.write32(cpu.gpr[3] + 8, voice.volume, cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceSrc",
        [this](CPUContext& cpu, GuestMemory& memory) {
            ax_voice(cpu.gpr[3]).ratio = memory.read32(cpu.gpr[4], cpu.pc);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceSrcRatio",
        [this](CPUContext& cpu, GuestMemory&) {
            const double ratio = std::bit_cast<double>(cpu.fpr[1][0]);
            if (ratio < 0.0) {
                cpu.gpr[3] = static_cast<uint32_t>(-1);
                return HleAction::return_to_lr;
            }
            ax_voice(cpu.gpr[3]).ratio = static_cast<uint32_t>(
                std::min<double>(ratio * 65536.0, UINT32_MAX));
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceAdpcm",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            for (uint32_t index = 0; index < voice.coefficients.size();
                 ++index) {
                voice.coefficients[index] = static_cast<int16_t>(
                    memory.read16(cpu.gpr[4] + index * 2, cpu.pc));
            }
            voice.pred_scale = memory.read16(cpu.gpr[4] + 0x22, cpu.pc);
            voice.previous[0] = static_cast<int16_t>(
                memory.read16(cpu.gpr[4] + 0x24, cpu.pc));
            voice.previous[1] = static_cast<int16_t>(
                memory.read16(cpu.gpr[4] + 0x26, cpu.pc));
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXSetVoiceAdpcmLoop",
        [this](CPUContext& cpu, GuestMemory& memory) {
            auto& voice = ax_voice(cpu.gpr[3]);
            voice.loop_pred_scale = memory.read16(cpu.gpr[4], cpu.pc);
            voice.loop_previous[0] = static_cast<int16_t>(
                memory.read16(cpu.gpr[4] + 2, cpu.pc));
            voice.loop_previous[1] = static_cast<int16_t>(
                memory.read16(cpu.gpr[4] + 4, cpu.pc));
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXRegisterDeviceFinalMixCallback",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t device = cpu.gpr[3];
            const uint32_t callback = cpu.gpr[4];
            if (device >= final_mix_callbacks_.size()) {
                throw GuestFault("invalid AX device", device, 4, cpu.pc,
                                 MemoryAccess::read);
            }
            if (callback != 0) {
                if ((callback & 3) != 0) {
                    throw GuestFault("invalid AX callback", callback, 4,
                                     cpu.pc, MemoryAccess::execute);
                }
                memory.validate(callback, 4, cpu.pc, MemoryAccess::execute);
            }
            const uint32_t data = kAxFinalMixData[device];
            const uint16_t channels = kAxChannels[device];
            const uint16_t devices = kAxDevices[device];
            memory.write32(data, data + 0x10, cpu.pc);
            memory.write16(data + 4, channels, cpu.pc);
            memory.write16(data + 6, kAxSamples, cpu.pc);
            memory.write16(data + 8, devices, cpu.pc);
            memory.write16(data + 10, channels, cpu.pc);
            for (uint32_t slot = 0; slot < channels * devices; ++slot) {
                const uint32_t samples =
                    data + kAxSamplesOffset + slot * kAxSampleStride;
                memory.write32(data + 0x10 + slot * 4, samples, cpu.pc);
                for (uint32_t index = 0; index < kAxSamples; ++index) {
                    memory.write32(samples + index * 4, 0, cpu.pc);
                }
            }
            final_mix_callbacks_[device] = callback;
            if (machine_ != nullptr) {
                machine_->queue_callback(callback, data);
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXRegisterAppFrameCallback",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t callback = cpu.gpr[3];
            if (callback == 0 || (callback & 3) != 0) {
                cpu.gpr[3] = static_cast<uint32_t>(-8);
                return HleAction::return_to_lr;
            }
            memory.validate(callback, 4, cpu.pc, MemoryAccess::execute);
            app_frame_callback_ = callback;
            if (machine_ != nullptr) {
                machine_->queue_callback(callback, 0);
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXUserBegin",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = static_cast<uint32_t>(audio_protect_depth_++);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXUserEnd",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = static_cast<uint32_t>(audio_protect_depth_--);
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXUserIsProtected",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = audio_protect_depth_ > 0;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXGetDeviceFinalMixCallback",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t device = cpu.gpr[3];
            if (device >= final_mix_callbacks_.size()) {
                throw GuestFault("invalid AX device", device, 4, cpu.pc,
                                 MemoryAccess::read);
            }
            if (cpu.gpr[4] != 0) {
                memory.write32(cpu.gpr[4], final_mix_callbacks_[device],
                               cpu.pc);
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    register_handler(
        "snd_core", "AXDeregisterAppFrameCallback",
        [this](CPUContext& cpu, GuestMemory&) {
            if (app_frame_callback_ == cpu.gpr[3]) {
                app_frame_callback_ = 0;
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    const char* const audio_success[] = {
        "AXSetDefaultMixerSelect", "AXSetDeviceLinearUpsampler",
        "AXSetDeviceUpsampleStage", "AXSetDeviceCompressor",
        "AXSetDRCVSMode", "AXSetDRCVSLC",
        "AXSetDRCVSDownmixBalance", "AXSetDRCVSLimiter",
        "AXSetDRCVSLimiterThreshold", "AXSetDRCVSOutputGain",
        "AXSetDRCVSSpeakerPosition", "AXSetDRCVSSurroundDepth",
        "AXSetDRCVSSurroundLevelGain", "AXGetSwapProfile",
        "AXRegisterAuxCallback", "AXSetAuxReturnVolume",
        "DRCVS_Process", "DRCVS_SetOutputMode"};
    for (const char* symbol : audio_success) {
        register_handler("snd_core", symbol,
                         [](CPUContext& cpu, GuestMemory&) {
                             cpu.gpr[3] = 0;
                             return HleAction::return_to_lr;
                         });
    }
    const char* const audio_void[] = {
        "AXComputeLpfCoefs", "AXDecodeAdpcmData", "AXRmtAdvancePtr",
        "AXSetVoiceBiquad", "AXSetVoiceBiquadCoefs", "AXSetVoiceLpf",
        "AXSetVoiceLpfCoefs", "AXSetVoiceRmtIIR",
        "AXSetVoiceRmtIIRCoefs", "AXSetVoiceRmtOn",
        "AXSetVoiceSrcType"};
    for (const char* symbol : audio_void) {
        register_handler(
            "snd_core", symbol,
            [](CPUContext&, GuestMemory&) {
                return HleAction::return_to_lr;
            });
    }
    for (const char* symbol :
         {"AXRmtGetSamples", "AXRmtGetSamplesLeft"}) {
        register_handler(
            "snd_core", symbol,
            [](CPUContext& cpu, GuestMemory&) {
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            });
    }
    const char* const audio_zero_output[] = {"AXGetDeviceMode"};
    for (const char* symbol : audio_zero_output) {
        register_handler(
            "snd_core", symbol,
            [](CPUContext& cpu, GuestMemory& memory) {
                if (cpu.gpr[4] != 0) {
                    memory.write32(cpu.gpr[4], 0, cpu.pc);
                }
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            });
    }
    register_handler(
        "vpad", "VPADRead",
        [this](CPUContext& cpu, GuestMemory& memory) {
            constexpr uint32_t kVpadStatusSize = 0xAC;
            uint32_t samples = 0;
            uint32_t error = 0xFFFFFFFF;
            if (cpu.gpr[5] != 0) {
                if (cpu.gpr[3] < 2) {
                    memory.validate_range(cpu.gpr[4], kVpadStatusSize, cpu.pc,
                                          MemoryAccess::write);
                    for (uint32_t offset = 0; offset < kVpadStatusSize;
                         ++offset) {
                        memory.write8(cpu.gpr[4] + offset, 0, cpu.pc);
                    }
                    if (cpu.gpr[3] == 0 && vpad_connected_) {
                        memory.write32(cpu.gpr[4], vpad_buttons_, cpu.pc);
                        memory.write32(cpu.gpr[4] + 4,
                                       vpad_buttons_ & ~vpad_last_buttons_,
                                       cpu.pc);
                        memory.write32(cpu.gpr[4] + 8,
                                       vpad_last_buttons_ & ~vpad_buttons_,
                                       cpu.pc);
                        memory.write32(cpu.gpr[4] + 0x10, vpad_buttons_, cpu.pc);
                        memory.write32(cpu.gpr[4] + 0x14,
                                       vpad_buttons_ & ~vpad_last_buttons_,
                                       cpu.pc);
                        memory.write32(cpu.gpr[4] + 0x18,
                                       vpad_last_buttons_ & ~vpad_buttons_,
                                       cpu.pc);
                        memory.write16(cpu.gpr[4] + 0x58, 3, cpu.pc);
                        memory.write16(cpu.gpr[4] + 0x60, 3, cpu.pc);
                        memory.write16(cpu.gpr[4] + 0x68, 3, cpu.pc);
                        memory.write8(cpu.gpr[4] + 0xA1, 4, cpu.pc);
                        vpad_last_buttons_ = vpad_buttons_;
                        samples = 1;
                        error = 0;
                    } else {
                        error = 0xFFFFFFFE;
                    }
                } else {
                    error = 0xFFFFFFFE;
                }
            }
            if (cpu.gpr[6] != 0) {
                memory.write32(cpu.gpr[6], error, cpu.pc);
            }
            cpu.gpr[3] = samples;
            return HleAction::return_to_lr;
        });
    for (const char* symbol : {"Initialize__Q2_2nn2acFv",
                               "Connect__Q2_2nn2acFv",
                               "Finalize__Q2_2nn2acFv",
                               "Close__Q2_2nn2acFv"}) {
        register_handler(
            "nn_ac", symbol,
            [](CPUContext& cpu, GuestMemory&) {
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            });
    }
    register_handler(
        "nn_ac", "GetLastErrorCode__Q2_2nn2acFPUi",
        [](CPUContext& cpu, GuestMemory& memory) {
            if (cpu.gpr[3] != 0) {
                memory.write32(cpu.gpr[3], UINT32_MAX, cpu.pc);
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    for (const char* symbol : {"socket_lib_init", "socket_lib_finish",
                               "NSSLInit", "NSSLFinish"}) {
        register_handler(
            "nsysnet", symbol,
            [](CPUContext& cpu, GuestMemory&) {
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            });
    }
    for (const char* symbol : {"curl_global_init_mem",
                               "curl_global_cleanup"}) {
        register_handler(
            "nlibcurl", symbol,
            [](CPUContext& cpu, GuestMemory&) {
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            });
    }
    register_handler(
        "vpad", "VPADGetTPCalibratedPoint",
        [](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t output = cpu.gpr[4];
            const uint32_t input = cpu.gpr[5];
            memory.validate_range(output, 8, cpu.pc, MemoryAccess::write);
            memory.validate_range(input, 8, cpu.pc, MemoryAccess::read);
            const uint16_t x = memory.read16(input, cpu.pc);
            const uint16_t y = memory.read16(input + 2, cpu.pc);
            const uint16_t touched = memory.read16(input + 4, cpu.pc);
            const uint16_t validity = memory.read16(input + 6, cpu.pc);
            memory.write16(output,
                           static_cast<uint16_t>(static_cast<float>(x) *
                                                 (1280.0f / 4096.0f)),
                           cpu.pc);
            memory.write16(output + 2,
                           static_cast<uint16_t>((4096.0f - y) *
                                                 (720.0f / 4096.0f)),
                           cpu.pc);
            memory.write16(output + 4, touched, cpu.pc);
            memory.write16(output + 6, validity, cpu.pc);
            return HleAction::return_to_lr;
        });
    for (const char* symbol :
         {"Initialize__Q2_2nn4bossFv", "Finalize__Q2_2nn4bossFv",
          "Initialize__Q3_2nn4boss17PlayReportSettingFPvUi",
          "Initialize__Q3_2nn4boss4TaskFPCcUi",
          "Register__Q3_2nn4boss4TaskFRQ3_2nn4boss11TaskSetting",
          "Set__Q3_2nn4boss17PlayReportSettingFUiT1",
          "StartScheduling__Q3_2nn4boss4TaskFb",
          "Unregister__Q3_2nn4boss4TaskFv"}) {
        register_handler(
            "nn_boss", symbol,
            [](CPUContext& cpu, GuestMemory&) {
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            });
    }
    register_handler(
        "nn_boss", "IsInitialized__Q2_2nn4bossFv",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    register_handler(
        "nn_boss", "IsRegistered__Q3_2nn4boss4TaskCFv",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    for (const char* symbol :
         {"__ct__Q3_2nn4boss17PlayReportSettingFv",
          "__ct__Q3_2nn4boss4TaskFv",
          "__dt__Q3_2nn4boss17PlayReportSettingFv",
          "__dt__Q3_2nn4boss4TaskFv"}) {
        register_handler(
            "nn_boss", symbol,
            [](CPUContext&, GuestMemory&) {
                return HleAction::return_to_lr;
            });
    }
}

CafeRuntime::AxVoice& CafeRuntime::ax_voice(uint32_t address) {
    if (address < kAxVoiceBase ||
        address >= kAxVoiceBase + ax_voices_.size() * kAxVoiceSize ||
        (address - kAxVoiceBase) % kAxVoiceSize != 0) {
        throw GuestFault("invalid AX voice", address, kAxVoiceSize, 0,
                         MemoryAccess::read);
    }
    auto& voice = ax_voices_[(address - kAxVoiceBase) / kAxVoiceSize];
    if (!voice.acquired) {
        throw GuestFault("unacquired AX voice", address, kAxVoiceSize, 0,
                         MemoryAccess::read);
    }
    return voice;
}

int16_t CafeRuntime::decode_audio_sample(AxVoice& voice) {
    if (voice.data == 0) {
        return 0;
    }
    if (voice.format == 0x0A) {
        return static_cast<int16_t>(
            image_.memory.read16(voice.data + voice.current_offset * 2, 0));
    }
    if (voice.format == 0x19) {
        return static_cast<int16_t>(
            image_.memory.read8(voice.data + voice.current_offset, 0) << 8);
    }
    if (voice.format != 0) {
        return 0;
    }
    if ((voice.current_offset & 0xF) < 2) {
        voice.pred_scale =
            image_.memory.read8(voice.data + voice.current_offset / 2, 0);
        voice.current_offset += 2;
    }
    const uint8_t packed =
        image_.memory.read8(voice.data + voice.current_offset / 2, 0);
    int32_t nibble =
        (voice.current_offset & 1) == 0 ? packed >> 4 : packed & 0xF;
    if (nibble >= 8) {
        nibble -= 16;
    }
    const uint32_t coefficient = (voice.pred_scale >> 4) & 7;
    const int32_t decoded =
        ((1 << (voice.pred_scale & 0xF)) * nibble) +
        ((0x400 + voice.coefficients[coefficient * 2] * voice.previous[0] +
          voice.coefficients[coefficient * 2 + 1] * voice.previous[1]) >>
         11);
    return static_cast<int16_t>(
        std::clamp(decoded, int32_t{INT16_MIN}, int32_t{INT16_MAX}));
}

bool CafeRuntime::advance_audio_voice(AxVoice& voice) {
    const int16_t sample = decode_audio_sample(voice);
    voice.previous[1] = voice.previous[0];
    voice.previous[0] = sample;
    if (voice.current_offset == voice.end_offset) {
        ++voice.loop_count;
        if (!voice.looping) {
            voice.playing = false;
            return false;
        }
        voice.current_offset = voice.loop_offset;
        voice.pred_scale = voice.loop_pred_scale;
        if (!voice.streaming) {
            voice.previous = voice.loop_previous;
        }
        return true;
    }
    ++voice.current_offset;
    if (voice.format == 0 && (voice.current_offset & 0xF) < 2) {
        voice.pred_scale =
            image_.memory.read8(voice.data + voice.current_offset / 2, 0);
        voice.current_offset += 2;
    }
    return true;
}

void CafeRuntime::mix_audio_frame() {
    std::array<int64_t, kAudioFrameSamples> mixed{};
    for (uint32_t index = 0; index < ax_voices_.size(); ++index) {
        auto& voice = ax_voices_[index];
        if (!voice.acquired || !voice.playing) {
            continue;
        }
        for (uint32_t sample_index = 0;
             sample_index < kAxSamples && voice.playing; ++sample_index) {
            const int64_t sample =
                static_cast<int64_t>(decode_audio_sample(voice)) *
                voice.volume / 0x8000;
            mixed[sample_index * 2] += sample * voice.mix[0] / 0x8000;
            mixed[sample_index * 2 + 1] += sample * voice.mix[1] / 0x8000;
            voice.volume = static_cast<uint16_t>(std::clamp<int32_t>(
                static_cast<int32_t>(voice.volume) + voice.volume_delta, 0,
                UINT16_MAX));
            voice.fraction += voice.ratio;
            while (voice.fraction >= 0x10000 && voice.playing) {
                voice.fraction -= 0x10000;
                advance_audio_voice(voice);
            }
        }
        const uint32_t address = kAxVoiceBase + index * kAxVoiceSize;
        image_.memory.write32(address + 4, voice.playing, 0);
        image_.memory.write32(address + 0x40, voice.current_offset, 0);
    }
    for (uint32_t slot = 0; slot < kAxChannels[0] * kAxDevices[0]; ++slot) {
        const uint32_t samples =
            kAxFinalMixData[0] + kAxSamplesOffset + slot * kAxSampleStride;
        for (uint32_t index = 0; index < kAxSamples; ++index) {
            int32_t sample = 0;
            if (slot < 2) {
                sample = static_cast<int32_t>(std::clamp<int64_t>(
                    mixed[index * 2 + slot], INT16_MIN, INT16_MAX));
            }
            image_.memory.write32(samples + index * 4,
                                  static_cast<uint32_t>(sample), 0);
        }
    }
}

void CafeRuntime::prepare_audio_frame(uint32_t data) {
    if (data == kAxFinalMixData[0]) {
        mix_audio_frame();
    }
}

void CafeRuntime::capture_audio_frame(uint32_t data) {
    size_t device = 0;
    while (device < kAxFinalMixData.size() &&
           kAxFinalMixData[device] != data) {
        ++device;
    }
    if (device == kAxFinalMixData.size()) {
        return;
    }

    if (device == 0) {
        auto& frame = audio_frames_[audio_write_];
        for (uint32_t index = 0; index < kAxSamples; ++index) {
            for (uint32_t channel = 0; channel < 2; ++channel) {
                const uint32_t address =
                    data + kAxSamplesOffset +
                    channel * kAxSampleStride + index * 4;
                frame[index * 2 + channel] =
                    static_cast<int16_t>(image_.memory.read32(address, 0));
            }
        }
        audio_write_ = (audio_write_ + 1) % audio_frames_.size();
        if (audio_count_ == audio_frames_.size()) {
            audio_read_ = (audio_read_ + 1) % audio_frames_.size();
        } else {
            ++audio_count_;
        }
    }

    for (uint32_t slot = 0;
         slot < kAxChannels[device] * kAxDevices[device]; ++slot) {
        const uint32_t samples =
            data + kAxSamplesOffset + slot * kAxSampleStride;
        for (uint32_t index = 0; index < kAxSamples; ++index) {
            image_.memory.write32(samples + index * 4, 0, 0);
        }
    }
}

bool CafeRuntime::pop_audio_frame(
    std::array<int16_t, kAudioFrameSamples>& samples) {
    if (audio_count_ == 0) {
        return false;
    }
    samples = audio_frames_[audio_read_];
    audio_read_ = (audio_read_ + 1) % audio_frames_.size();
    --audio_count_;
    return true;
}

void CafeRuntime::service_audio_frame() {
    if (machine_ == nullptr) {
        return;
    }
    audio_sample_phase_ += 800;
    while (audio_sample_phase_ >= kAxSamples) {
        audio_sample_phase_ -= kAxSamples;
        machine_->queue_callback(app_frame_callback_, 0, true);
        for (size_t device = 0; device < final_mix_callbacks_.size();
             ++device) {
            machine_->queue_callback(final_mix_callbacks_[device],
                                     kAxFinalMixData[device], true);
        }
    }
}

void CafeRuntime::register_handler(std::string module, std::string symbol,
                                   HleHandler handler) {
    const bool records_display_command =
        module == "gx2" && symbol != "GX2BeginDisplayListEx" &&
        symbol != "GX2GetCurrentDisplayList" &&
        symbol != "GX2EndDisplayList";
    if (records_display_command) {
        const uint32_t command =
            gx2_.register_display_list_handler(symbol, handler);
        handler = [this, command, handler = std::move(handler)](
                      CPUContext& cpu, GuestMemory& memory) {
            const CPUContext recorded = cpu;
            if (gx2_.record_display_list_command(command, recorded)) {
                return HleAction::return_to_lr;
            }
            return handler(cpu, memory);
        };
    }
    auto module_it = handlers_.try_emplace(std::move(module)).first;
    const auto [handler_it, inserted] =
        module_it->second.emplace(std::move(symbol), std::move(handler));
    if (!inserted) {
        throw std::invalid_argument("Cafe handler pair already registered: " +
                                    module_it->first + "::" +
                                    handler_it->first);
    }
}

void CafeRuntime::register_imports(Executor& executor) const {
    for (const auto& [address, target] : image_.imports) {
        const auto module = handlers_.find(target.module);
        if (module == handlers_.end()) {
            continue;
        }
        const auto handler = module->second.find(target.symbol);
        if (handler != module->second.end()) {
            executor.register_hle(address, handler->second);
        }
    }
}
} // namespace nwii::runtime
