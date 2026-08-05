#pragma once

#include "runtime/execution_image.h"
#include "runtime/executor.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace nwii::runtime {
class CafeRuntime;

class CafeFilesystem {
public:
    CafeFilesystem(ExecutionImage& image, std::filesystem::path title_root,
                   std::optional<std::filesystem::path> save_root);
    CafeFilesystem(const CafeFilesystem&) = delete;
    CafeFilesystem& operator=(const CafeFilesystem&) = delete;
    CafeFilesystem(CafeFilesystem&&) = delete;
    CafeFilesystem& operator=(CafeFilesystem&&) = delete;

    void register_handlers(CafeRuntime& runtime);

    std::optional<std::filesystem::path>
    resolve_content_path(std::string_view guest_path) const noexcept;

private:
    struct OpenMode {
        bool read{};
        bool write{};
        bool truncate{};
        bool append{};
        bool create{};
    };

    struct File {
        uint32_t client{};
        std::filesystem::path path;
        std::fstream stream;
        uint64_t position{};
        bool read{};
        bool write{};
        bool append{};
        bool namespace_writable{};
    };

    struct DirectoryEntry {
        std::string name;
        uint32_t flags{};
        uint32_t mode{};
        uint32_t size{};
    };

    struct Directory {
        uint32_t client{};
        std::vector<DirectoryEntry> entries;
        size_t position{};
    };

    struct ResolvedPath {
        std::filesystem::path path;
        bool writable{};
    };

    static bool contains(const std::filesystem::path& root,
                         const std::filesystem::path& candidate);
    static bool invalid_relative(std::string_view path);
    static std::optional<OpenMode> parse_mode(std::string_view mode);
    static int32_t error_from_code(const std::error_code& error);
    static std::filesystem::path canonical_root(
        const std::filesystem::path& path) noexcept;

    std::optional<std::filesystem::path> resolve_under(
        const std::filesystem::path& root, std::string_view relative,
        bool allow_missing, int32_t& detail) const noexcept;
    std::optional<ResolvedPath> resolve_content(
        std::string_view guest_path, bool allow_missing,
        int32_t& detail) const noexcept;
    std::optional<ResolvedPath> resolve_save(uint8_t slot,
                                             std::string_view guest_path,
                                             bool allow_missing,
                                             int32_t& detail) const noexcept;
    std::optional<std::filesystem::path> save_slot(uint8_t slot,
                                                   int32_t& detail) const;

    std::optional<std::string> guest_string(uint32_t address,
                                            uint32_t limit,
                                            uint32_t pc) const;
    void write_stat(uint32_t address, const std::filesystem::path& path,
                    bool writable, uint32_t pc);
    void write_stat(uint32_t address, const DirectoryEntry& entry,
                    uint32_t pc);
    void write_directory_entry(uint32_t address,
                               const DirectoryEntry& entry, uint32_t pc);
    std::optional<std::vector<DirectoryEntry>> snapshot_directory(
        const std::filesystem::path& path, bool writable,
        int32_t& detail) const;

    int32_t finish(uint32_t client, int32_t detail, uint32_t error_mask);
    int32_t success(uint32_t client, int32_t result = 0);
    std::optional<int32_t> command_failure(uint32_t client, uint32_t command,
                                           uint32_t error_mask);
    int32_t open_file(uint32_t client, uint32_t command,
                      std::string_view guest_path, std::string_view mode,
                      uint32_t output_handle, uint32_t error_mask,
                      bool save, uint8_t slot, uint32_t pc);
    int32_t open_directory(uint32_t client, uint32_t command,
                           std::string_view guest_path,
                           uint32_t output_handle, uint32_t error_mask,
                           bool save, uint8_t slot, uint32_t pc);
    void close_client_handles(uint32_t client);
    uint32_t allocate_handle();

    ExecutionImage& image_;
    GuestMemory& memory_;
    std::filesystem::path content_root_;
    std::optional<std::filesystem::path> save_root_;
    bool fs_initialized_{};
    bool save_initialized_{};
    uint32_t next_handle_{1};
    std::set<uint32_t> clients_;
    std::set<uint32_t> commands_;
    std::map<uint32_t, uint32_t> command_priorities_;
    std::map<uint32_t, int32_t> last_errors_;
    std::map<uint32_t, std::array<uint32_t, 3>> notifications_;
    std::map<uint32_t, File> files_;
    std::map<uint32_t, Directory> directories_;
};
} // namespace nwii::runtime
