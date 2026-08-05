#include "nwiiu/recomp/runner_cli.h"

#include <charconv>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace nwiiu::recomp {
namespace {
std::string_view category_name(nwii::runtime::StopCategory category) {
    using nwii::runtime::StopCategory;
    switch (category) {
    case StopCategory::input_error:
        return "input_error";
    case StopCategory::guest_fault:
        return "guest_fault";
    case StopCategory::missing_hle:
        return "missing_hle";
    case StopCategory::instruction_budget:
        return "instruction_budget";
    case StopCategory::guest_exit:
        return "guest_exit";
    case StopCategory::deadlock:
        return "deadlock";
    }
    throw std::invalid_argument("unknown execution stop category");
}

void append_address(std::ostringstream& output, uint32_t address) {
    output << "0x" << std::uppercase << std::hex << std::setfill('0')
           << std::setw(8) << address;
}

std::string_view access_name(nwii::runtime::MemoryAccess access) {
    using nwii::runtime::MemoryAccess;
    switch (access) {
    case MemoryAccess::read:
        return "read";
    case MemoryAccess::write:
        return "write";
    case MemoryAccess::execute:
        return "execute";
    }
    throw std::invalid_argument("unknown guest memory access");
}
} // namespace

RunnerOptions parse_runner_options(std::span<const std::string_view> args) {
    if (args.empty() || args[0].empty()) {
        throw std::invalid_argument(
            "usage: nwiiu-run <input.rpx> --max-instructions <positive> "
            "[--config <profile.toml>] [--save-root <dir>] "
            "[--shared-font <ttf>] [--trace] [--window]");
    }

    std::optional<uint64_t> max_instructions;
    std::optional<std::filesystem::path> save_root;
    std::optional<std::filesystem::path> shared_font;
    std::optional<std::filesystem::path> config;
    bool trace = false;
    bool window = false;
    for (size_t index = 1; index < args.size(); ++index) {
        const auto flag = args[index];
        if (flag == "--trace") {
            if (trace) {
                throw std::invalid_argument("duplicate --trace option");
            }
            trace = true;
            continue;
        }
        if (flag == "--window") {
            if (window) {
                throw std::invalid_argument("duplicate --window option");
            }
            window = true;
            continue;
        }
        if (flag != "--max-instructions" && flag != "--save-root" &&
            flag != "--shared-font" && flag != "--config") {
            throw std::invalid_argument("unknown runner option");
        }
        if (++index >= args.size()) {
            throw std::invalid_argument("runner option requires a value");
        }
        const auto value = args[index];
        if (flag == "--max-instructions") {
            if (max_instructions) {
                throw std::invalid_argument(
                    "duplicate --max-instructions option");
            }
            uint64_t parsed{};
            const auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} ||
                end != value.data() + value.size() || parsed == 0) {
                throw std::invalid_argument(
                    "--max-instructions must be a positive integer");
            }
            max_instructions = parsed;
        } else if (flag == "--save-root") {
            if (save_root || value.empty()) {
                throw std::invalid_argument(
                    "--save-root must be specified once with a nonempty path");
            }
            save_root = std::filesystem::path{std::string{value}};
        } else if (flag == "--config") {
            if (config || value.empty()) {
                throw std::invalid_argument(
                    "--config must be specified once with a nonempty path");
            }
            config = std::filesystem::path{std::string{value}};
        } else {
            if (shared_font || value.empty()) {
                throw std::invalid_argument(
                    "--shared-font must be specified once with a nonempty path");
            }
            shared_font = std::filesystem::path{std::string{value}};
        }
    }
    if (!max_instructions) {
        throw std::invalid_argument("--max-instructions is required");
    }

    std::filesystem::path input{std::string{args[0]}};
    return {input,
            input.parent_path().parent_path(),
            std::move(save_root),
            std::move(shared_font),
            std::move(config),
            *max_instructions,
            trace,
            window};
}

std::string format_stop(const nwii::runtime::ExecutionStop& stop) {
    std::ostringstream output;
    const std::string_view reason =
        stop.category == nwii::runtime::StopCategory::instruction_budget &&
                stop.reason == "global instruction budget exhausted"
            ? std::string_view{"instruction budget exhausted"}
            : std::string_view{stop.reason};
    output << "STOP " << category_name(stop.category) << ": " << reason
           << "\nPC: ";
    append_address(output, stop.pc);
    output << "\nLR: ";
    append_address(output, stop.lr);
    output << "\nInstructions: " << std::dec << stop.instruction_count << '\n';

    if (stop.category == nwii::runtime::StopCategory::guest_fault) {
        output << "Address: ";
        append_address(output, stop.fault_address);
        output << "\nWidth: " << std::dec << stop.fault_width
               << "\nAccess: " << access_name(stop.fault_access)
               << "\nInstruction: ";
        if (stop.raw_instruction.has_value()) {
            append_address(output, *stop.raw_instruction);
        } else {
            output << "unavailable";
        }
        output << '\n';
    } else if (stop.category == nwii::runtime::StopCategory::missing_hle) {
        output << "Import: " << stop.module << ':' << stop.symbol
               << "\nArguments:";
        for (size_t index = 0; index < stop.argument_gprs.size(); ++index) {
            output << " r" << std::dec << index + 3 << '=';
            append_address(output, stop.argument_gprs[index]);
        }
        output << '\n';
    }

    output << "History:";
    for (size_t index = 0; index < stop.history_size; ++index) {
        output << ' ';
        append_address(output, stop.history[index]);
    }
    output << '\n';
    return output.str();
}

std::string format_trace(const nwii::runtime::ExecutionStop& stop) {
    std::ostringstream output;
    output << "TRACE active-thread: ";
    append_address(output, stop.active_thread);
    output << "\nTRACE HLE calls (oldest first, last "
           << std::dec << nwii::runtime::kHleTraceCapacity;
    if (stop.hle_trace_truncated) {
        output << ", truncated";
    }
    output << "):\n";
    for (size_t index = 0; index < stop.hle_call_count; ++index) {
        const auto& call = stop.hle_calls[index];
        const auto append_call = [&](std::string_view phase) {
            output << "  " << phase << " thread=";
            append_address(output, call.thread_id);
            output << " import=" << call.module << ':' << call.symbol
                   << " address=";
            append_address(output, call.address);
            output << '\n';
        };
        append_call("ENTER");
        if (call.returned) {
            append_call("RETURN");
        }
    }
    return output.str();
}
} // namespace nwiiu::recomp
