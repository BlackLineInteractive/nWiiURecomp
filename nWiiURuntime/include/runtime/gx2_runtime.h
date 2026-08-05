#pragma once
#include <array>
#include "runtime/executor.h"
#include <cstddef>
#include <map>

#include <span>
#include <vector>
#include <utility>
#include <cstdint>

namespace nwii::runtime {
class CafeRuntime;
class ExecutionImage;
class GuestMemory;
class Machine;

enum class Gx2Opcode : uint16_t {
    load_context = 1,
};

template <std::size_t Count> struct Gx2ScalarState {
    bool valid{};
    std::array<uint32_t, Count> args{};

    bool operator==(const Gx2ScalarState&) const = default;
};

struct Gx2ScanBufferState {
    bool valid{};
    uint32_t address{};
    uint32_t size{};
    uint32_t mode{};
    uint32_t format{};
    uint32_t buffering{};
    uint32_t width{};
    uint32_t height{};
    uint32_t pitch{};

    bool operator==(const Gx2ScanBufferState&) const = default;
};

struct Gx2AttribBufferState {
    bool valid{};
    uint32_t address{};
    uint32_t size{};
    uint32_t stride{};

    bool operator==(const Gx2AttribBufferState&) const = default;
};
struct Gx2UniformBlockState {
    bool valid{};
    uint32_t address{};
    uint32_t size{};

    bool operator==(const Gx2UniformBlockState&) const = default;
};


struct Gx2DrawState {
    bool valid{};
    bool indexed{};
    uint32_t mode{};
    uint32_t count{};
    uint32_t index_type{};
    uint32_t indices{};
    uint32_t offset{};
    uint32_t instances{};

    bool operator==(const Gx2DrawState&) const = default;
};

struct Gx2DisplayListState {
    uint32_t address{};
    uint32_t capacity{};
    uint32_t size{};
    bool profiling{};

    bool operator==(const Gx2DisplayListState&) const = default;
};

struct Gx2DisplayListContext {
    bool active{};
    uint32_t address{};
    uint32_t capacity{};
    uint32_t size{};
    bool profiling{};
    std::array<Gx2DisplayListState, 4> stack{};
    uint32_t depth{};

    bool operator==(const Gx2DisplayListContext&) const = default;
};

struct Gx2InvalidateState {
    bool valid{};
    uint32_t count{};
    uint32_t mode{};
    uint32_t buffer{};
    uint32_t size{};
    uint32_t aligned_size{};

    bool operator==(const Gx2InvalidateState&) const = default;
};

struct Gx2State {
    bool initialized{};
    uint32_t main_core_id{0xFFFFFFFFu};
    uint32_t command_buffer_base{};
    uint32_t command_buffer_size{};
    uint32_t app_io_stack_base{};
    uint32_t app_io_stack_size{0x1000};
    uint32_t argc{};
    uint32_t argv{};
    uint32_t profile_mode{};
    uint32_t toss_stage{};
    bool events_initialized{};
    bool flip_callback_installed{};
    bool command_buffer_pool_initialized{};
    bool default_state_initialized{};
    uint32_t flush_count{};
    Gx2ScalarState<13> depth_stencil;
    Gx2ScalarState<6> stencil_mask;
    Gx2ScalarState<9> polygon_control;
    Gx2ScalarState<7> shader_mode;
    bool vertex_uniforms_valid{};
    bool pixel_uniforms_valid{};
    std::array<uint32_t, 1024> vertex_uniform_registers{};
    std::array<uint32_t, 1024> pixel_uniform_registers{};
    std::array<Gx2UniformBlockState, 16> vertex_uniform_blocks{};
    std::array<Gx2UniformBlockState, 16> pixel_uniform_blocks{};
    Gx2ScalarState<4> color_control;
    std::array<Gx2ScalarState<8>, 8> blend_controls;
    std::array<Gx2ScalarState<2>, 8> color_buffers;
    uint32_t depth_buffer_address{};
    Gx2ScalarState<5> last_color_clear;
    uint64_t color_clear_count{};
    Gx2ScalarState<4> last_depth_stencil_clear;
    uint64_t depth_stencil_clear_count{};
    Gx2ScalarState<4> blend_constant;
    Gx2ScalarState<3> alpha_test;
    Gx2ScalarState<8> target_channel_masks;
    Gx2ScalarState<2> alpha_to_mask;
    Gx2ScalarState<2> context_setup;
    Gx2ScalarState<6> viewport;
    Gx2ScalarState<4> scissor;
    uint32_t context_state_address{};
    Gx2ScanBufferState tv_scan_buffer;
    Gx2ScanBufferState drc_scan_buffer;
    bool tv_enabled{};
    bool drc_enabled{};
    Gx2ScalarState<2> last_scan_copy;
    uint64_t scan_copy_count{};
    Gx2ScalarState<2> tv_scale;
    Gx2ScalarState<2> drc_scale;
    uint32_t swap_interval{1};
    uint64_t vsync_wait_count{};
    uint32_t swap_count{};
    uint32_t flip_count{};
    uint64_t last_flip{};
    uint64_t last_vsync{};
    std::array<Gx2DisplayListContext, 3> display_lists{};
    uint32_t vertex_shader_address{};
    uint32_t pixel_shader_address{};
    uint32_t fetch_shader_address{};
    std::array<uint32_t, 32> pixel_texture_addresses{};
    std::array<Gx2AttribBufferState, 16> attribute_buffers{};
    uint64_t draw_count{};
    Gx2DrawState last_draw;
    std::array<uint32_t, 18> pixel_sampler_addresses{};
    Gx2InvalidateState last_invalidate;

    bool operator==(const Gx2State&) const = default;
};

enum class Gx2Event {
    clear_color,
    draw,
    copy_scan_buffer,
    swap_scan_buffers,
};

using Gx2EventCallback = void (*)(void*, Gx2Event, const Gx2State&);

class Gx2Runtime {
public:
    explicit Gx2Runtime(ExecutionImage& image);
    Gx2Runtime(const Gx2Runtime&) = delete;
    Gx2Runtime& operator=(const Gx2Runtime&) = delete;
    Gx2Runtime(Gx2Runtime&&) = delete;
    Gx2Runtime& operator=(Gx2Runtime&&) = delete;

    void register_handlers(CafeRuntime& runtime);
    void attach_machine(Machine& machine) { machine_ = &machine; }
    void attach_event_callback(void* context, Gx2EventCallback callback) {
        event_context_ = context;
        event_callback_ = callback;
    }
    uint32_t register_display_list_handler(std::string name,
                                           HleHandler handler);
    bool record_display_list_command(uint32_t command,
                                     const CPUContext& cpu);
    bool copy_scan_buffer(uint32_t target, std::span<uint8_t> output) const;
    const Gx2State& state() const { return state_; }
    std::vector<std::pair<uint32_t, uint32_t>> take_texture_invalidates() {
        return std::exchange(pending_texture_invalidates_, {});
    }

private:
    struct RecordedDisplayListCommand {
        uint32_t command{};
        std::array<uint32_t, 8> gpr{};
        std::array<uint64_t, 8> fpr{};
        std::array<uint32_t, 5> stack{};
        std::vector<uint8_t> data;
    };
    struct RecordedDisplayList {
        uint32_t size{};
        uint32_t capacity{};
        uint32_t replay_address{};
        uint32_t replay_capacity{};
        std::vector<RecordedDisplayListCommand> commands;
    };
    Gx2DisplayListContext& current_display_list();
    void copy_color_buffer_to_scan(uint32_t descriptor,
                                   const Gx2ScanBufferState& scan,
                                   uint32_t pc);
    void replay_display_list(uint32_t address, uint32_t size, uint32_t pc);
    void notify(Gx2Event event) const {
        if (event_callback_ != nullptr) {
            event_callback_(event_context_, event, state_);
        }
    }
    Machine* machine_{};
    void* event_context_{};
    Gx2EventCallback event_callback_{};
    GuestMemory& memory_;
    Gx2State state_;
    std::vector<uint8_t> surface_scratch_;
    std::vector<uint8_t> scan_scratch_;
    std::vector<HleHandler> display_list_handlers_;
    std::vector<std::string> display_list_handler_names_;
    std::vector<uint8_t> display_list_stack_words_;
    std::vector<uint32_t> display_list_payload_bytes_;
    std::array<std::vector<RecordedDisplayListCommand>, 3>
        recording_display_lists_;
    std::array<std::array<std::vector<RecordedDisplayListCommand>, 4>, 3>
        recording_display_list_stack_;
    std::map<uint32_t, RecordedDisplayList> recorded_display_lists_;
    std::vector<std::pair<uint32_t, uint32_t>> pending_texture_invalidates_;
    uint32_t replay_data_cursor_{0x1000};
};
} // namespace nwii::runtime
