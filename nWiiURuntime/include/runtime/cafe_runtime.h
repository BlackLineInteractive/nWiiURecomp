#pragma once

#include "runtime/executor.h"
#include "runtime/cafe_proc_ui.h"
#include "runtime/cafe_padscore.h"
#include "runtime/cafe_act.h"
#include "runtime/gx2_runtime.h"

#include <array>
#include <map>
#include <string>

namespace nwii::runtime {
class Machine;
class CafeRuntime {
public:
    explicit CafeRuntime(ExecutionImage& image);
    void attach_machine(Machine& machine) {
        machine_ = &machine;
        gx2_.attach_machine(machine);
    }
    void register_handler(std::string module, std::string symbol,
                          HleHandler handler);
    void register_imports(Executor& executor) const;
    void service_audio_frame();
    static constexpr size_t kAudioFrameSamples = 144 * 2;
    void capture_audio_frame(uint32_t data);
    void prepare_audio_frame(uint32_t data);
    bool pop_audio_frame(
        std::array<int16_t, kAudioFrameSamples>& samples);
    void set_vpad_buttons(uint32_t buttons) {
        vpad_connected_ = true;
        vpad_buttons_ = buttons;
    }
    Gx2Runtime& gx2() { return gx2_; }
    const Gx2Runtime& gx2() const { return gx2_; }
    CafeProcUi& proc_ui() { return proc_ui_; }
    const CafeProcUi& proc_ui() const { return proc_ui_; }
    CafePadscore& padscore() { return padscore_; }
    const CafePadscore& padscore() const { return padscore_; }
    CafeAct& act() { return act_; }
    const CafeAct& act() const { return act_; }

private:
    struct AxVoice {
        bool acquired{};
        bool playing{};
        bool looping{};
        bool streaming{};
        uint16_t format{};
        uint16_t volume{0x8000};
        int16_t volume_delta{};
        uint32_t data{};
        uint32_t loop_offset{};
        uint32_t end_offset{};
        uint32_t current_offset{};
        uint32_t loop_count{};
        uint32_t ratio{0x10000};
        uint32_t fraction{};
        uint16_t pred_scale{};
        std::array<int16_t, 16> coefficients{};
        std::array<int16_t, 2> previous{};
        uint16_t loop_pred_scale{};
        std::array<int16_t, 2> loop_previous{};
        std::array<uint16_t, 2> mix{};
    };
    AxVoice& ax_voice(uint32_t address);
    int16_t decode_audio_sample(AxVoice& voice);
    bool advance_audio_voice(AxVoice& voice);
    void mix_audio_frame();
    ExecutionImage& image_;
    Machine* machine_{};
    std::array<uint32_t, 2> final_mix_callbacks_{};
    uint32_t app_frame_callback_{};
    int32_t audio_protect_depth_{};
    uint32_t audio_sample_phase_{};
    std::array<AxVoice, 96> ax_voices_{};
    std::array<std::array<int16_t, kAudioFrameSamples>, 16> audio_frames_{};
    size_t audio_read_{};
    size_t audio_write_{};
    size_t audio_count_{};
    bool vpad_connected_{};
    uint32_t vpad_buttons_{};
    uint32_t vpad_last_buttons_{};
    CafeProcUi proc_ui_;
    CafePadscore padscore_;
    CafeAct act_;
    Gx2Runtime gx2_;
    std::map<std::string, std::map<std::string, HleHandler>> handlers_;
};
} // namespace nwii::runtime
