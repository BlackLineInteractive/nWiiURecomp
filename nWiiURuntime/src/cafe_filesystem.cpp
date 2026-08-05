#include "runtime/cafe_filesystem.h"

#include "runtime/cafe_abi.h"
#include "runtime/cafe_runtime.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <limits>
#include <system_error>

namespace nwii::runtime {
namespace {
constexpr int32_t kStatusOk = 0;
constexpr int32_t kStatusEnd = -2;
constexpr int32_t kStatusAlreadyOpen = -4;
constexpr int32_t kStatusExists = -5;
constexpr int32_t kStatusNotFound = -6;
constexpr int32_t kStatusNotFile = -7;
constexpr int32_t kStatusNotDir = -8;
constexpr int32_t kStatusAccess = -9;
constexpr int32_t kStatusPermission = -10;
constexpr int32_t kStatusTooBig = -11;
constexpr int32_t kStatusFull = -12;
constexpr int32_t kStatusUnsupported = -14;
constexpr int32_t kStatusFatal = -0x400;

constexpr int32_t kErrorOk = 0;
constexpr int32_t kErrorNotInit = -0x30001;
constexpr int32_t kErrorEndDir = -0x30004;
constexpr int32_t kErrorEndFile = -0x30005;
constexpr int32_t kErrorAlreadyOpen = -0x30015;
constexpr int32_t kErrorExists = -0x30016;
constexpr int32_t kErrorNotFound = -0x30017;
constexpr int32_t kErrorAccess = -0x30019;
constexpr int32_t kErrorPermission = -0x3001A;
constexpr int32_t kErrorFull = -0x3001C;
constexpr int32_t kErrorUnsupported = -0x30020;
constexpr int32_t kErrorInvalidParam = -0x30021;
constexpr int32_t kErrorInvalidPath = -0x30022;
constexpr int32_t kErrorInvalidBuffer = -0x30023;
constexpr int32_t kErrorInvalidClient = -0x30025;
constexpr int32_t kErrorInvalidFile = -0x30026;
constexpr int32_t kErrorInvalidDir = -0x30027;
constexpr int32_t kErrorNotFile = -0x30028;
constexpr int32_t kErrorNotDir = -0x30029;
constexpr int32_t kErrorTooBig = -0x3002A;
constexpr int32_t kErrorOutOfRange = -0x3002B;
constexpr int32_t kErrorOutOfResources = -0x3002C;

constexpr uint32_t kFlagAlreadyOpen = 0x2;
constexpr uint32_t kFlagExists = 0x4;
constexpr uint32_t kFlagNotFound = 0x8;
constexpr uint32_t kFlagNotFile = 0x10;
constexpr uint32_t kFlagNotDir = 0x20;
constexpr uint32_t kFlagAccess = 0x40;
constexpr uint32_t kFlagPermission = 0x80;
constexpr uint32_t kFlagTooBig = 0x100;
constexpr uint32_t kFlagFull = 0x200;
constexpr uint32_t kFlagUnsupported = 0x400;

constexpr uint32_t kStatDirectory = 0x80000000;
constexpr uint32_t kStatFile = 0x01000000;
constexpr uint32_t kStatLink = 0x00010000;
constexpr uint32_t kPathLimit = 0x27F;
constexpr uint32_t kModeLimit = 0x10;
constexpr uint32_t kStatSize = 0x64;
constexpr uint32_t kDirectoryEntrySize = 0x164;

uint32_t as_guest(int32_t value) { return static_cast<uint32_t>(value); }

bool is_drive_path(std::string_view path) {
    return path.size() >= 2 &&
           ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
}
} // namespace

CafeFilesystem::CafeFilesystem(
    ExecutionImage& image, std::filesystem::path title_root,
    std::optional<std::filesystem::path> save_root)
    : image_(image), memory_(image.memory),
      content_root_(canonical_root(title_root / "content")) {
    if (save_root) {
        save_root_ = canonical_root(*save_root);
    }
}

std::filesystem::path CafeFilesystem::canonical_root(
    const std::filesystem::path& path) noexcept {
    try {
        std::error_code error;
        auto canonical = std::filesystem::weakly_canonical(path, error);
        if (!error) {
            return canonical;
        }
        canonical = std::filesystem::absolute(path, error);
        return error ? std::filesystem::path{} : canonical.lexically_normal();
    } catch (...) {
        return {};
    }
}

bool CafeFilesystem::contains(const std::filesystem::path& root,
                              const std::filesystem::path& candidate) {
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() || *root_part != *candidate_part) {
            return false;
        }
    }
    return true;
}

bool CafeFilesystem::invalid_relative(std::string_view path) {
    if (path.find('\0') != std::string_view::npos ||
        (!path.empty() && (path.front() == '/' || path.front() == '\\')) ||
        is_drive_path(path)) {
        return true;
    }
    try {
        const std::filesystem::path host_path{std::string{path}};
        if (host_path.is_absolute() || host_path.has_root_name() ||
            host_path.has_root_directory()) {
            return true;
        }
        return std::any_of(host_path.begin(), host_path.end(),
                           [](const auto& component) {
                               return component == "..";
                           });
    } catch (...) {
        return true;
    }
}

std::optional<CafeFilesystem::OpenMode>
CafeFilesystem::parse_mode(std::string_view mode) {
    if (mode.ends_with('b')) {
        mode.remove_suffix(1);
    }
    if (mode == "r") {
        return OpenMode{true, false, false, false, false};
    }
    if (mode == "r+") {
        return OpenMode{true, true, false, false, false};
    }
    if (mode == "w") {
        return OpenMode{false, true, true, false, true};
    }
    if (mode == "w+") {
        return OpenMode{true, true, true, false, true};
    }
    if (mode == "a") {
        return OpenMode{false, true, false, true, true};
    }
    if (mode == "a+") {
        return OpenMode{true, true, false, true, true};
    }
    return std::nullopt;
}

int32_t CafeFilesystem::error_from_code(const std::error_code& error) {
    if (!error) {
        return kErrorOk;
    }
    if (error == std::errc::no_such_file_or_directory) {
        return kErrorNotFound;
    }
    if (error == std::errc::not_a_directory) {
        return kErrorNotDir;
    }
    if (error == std::errc::file_exists) {
        return kErrorExists;
    }
    if (error == std::errc::permission_denied ||
        error == std::errc::read_only_file_system) {
        return kErrorPermission;
    }
    if (error == std::errc::no_space_on_device) {
        return kErrorFull;
    }
    if (error == std::errc::file_too_large) {
        return kErrorTooBig;
    }
    return kErrorAccess;
}

std::optional<std::filesystem::path> CafeFilesystem::resolve_under(
    const std::filesystem::path& root, std::string_view relative,
    bool allow_missing, int32_t& detail) const noexcept {
    detail = kErrorInvalidPath;
    if (root.empty() || invalid_relative(relative)) {
        return std::nullopt;
    }
    try {
        const std::filesystem::path lexical_relative{std::string{relative}};
        std::error_code error;
        auto resolved = root;
        for (const auto& component : lexical_relative) {
            if (component.empty() || component == ".") {
                continue;
            }
            auto next = resolved / component;
            if (!std::filesystem::exists(next, error)) {
                error.clear();
                const auto requested = component.string();
                for (std::filesystem::directory_iterator entry{resolved, error},
                     end;
                     !error && entry != end; entry.increment(error)) {
                    const auto actual = entry->path().filename().string();
                    if (requested.size() == actual.size() &&
                        std::equal(
                            requested.begin(), requested.end(), actual.begin(),
                            [](unsigned char lhs, unsigned char rhs) {
                                return std::tolower(lhs) == std::tolower(rhs);
                            })) {
                        next = entry->path();
                        break;
                    }
                }
                if (error == std::errc::no_such_file_or_directory) {
                    error.clear();
                } else if (error) {
                    detail = error_from_code(error);
                    return std::nullopt;
                }
            }
            resolved = std::move(next);
            const auto link_status =
                std::filesystem::symlink_status(resolved, error);
            if (error == std::errc::no_such_file_or_directory) {
                error.clear();
                continue;
            }
            if (error) {
                detail = error_from_code(error);
                return std::nullopt;
            }
            if (std::filesystem::is_symlink(link_status)) {
                const auto target_status =
                    std::filesystem::status(resolved, error);
                if (error) {
                    detail = error_from_code(error);
                    return std::nullopt;
                }
                if (std::filesystem::is_directory(target_status)) {
                    return std::nullopt;
                }
            }
        }

        const auto candidate =
            std::filesystem::weakly_canonical(resolved, error);
        if (error) {
            detail = error_from_code(error);
            return std::nullopt;
        }
        if (!contains(root, candidate)) {
            return std::nullopt;
        }
        if (!allow_missing && !std::filesystem::exists(candidate, error)) {
            detail = error ? error_from_code(error) : kErrorNotFound;
            return std::nullopt;
        }
        if (error) {
            detail = error_from_code(error);
            return std::nullopt;
        }
        detail = kErrorOk;
        return candidate;
    } catch (...) {
        detail = kErrorInvalidPath;
        return std::nullopt;
    }
}

std::optional<CafeFilesystem::ResolvedPath> CafeFilesystem::resolve_content(
    std::string_view guest_path, bool allow_missing,
    int32_t& detail) const noexcept {
    constexpr std::string_view prefix = "/vol/content";
    if (guest_path != prefix &&
        !(guest_path.size() > prefix.size() &&
          guest_path.substr(0, prefix.size()) == prefix &&
          guest_path[prefix.size()] == '/')) {
        detail = kErrorInvalidPath;
        return std::nullopt;
    }
    auto relative = guest_path.substr(prefix.size());
    while (!relative.empty() && relative.front() == '/') {
        relative.remove_prefix(1);
    }
    const auto path = resolve_under(content_root_, relative, allow_missing,
                                    detail);
    if (!path) {
        return std::nullopt;
    }
    return ResolvedPath{*path, false};
}

std::optional<std::filesystem::path> CafeFilesystem::resolve_content_path(
    std::string_view guest_path) const noexcept {
    int32_t detail{};
    const auto resolved = resolve_content(guest_path, false, detail);
    return resolved ? std::optional{resolved->path} : std::nullopt;
}

std::optional<std::filesystem::path> CafeFilesystem::save_slot(
    uint8_t slot, int32_t& detail) const {
    if (!save_root_) {
        detail = kErrorNotFound;
        return std::nullopt;
    }
    std::error_code error;
    const auto lexical =
        *save_root_ / ("slot-" + std::to_string(slot));
    const auto link_status = std::filesystem::symlink_status(lexical, error);
    if (!error && std::filesystem::is_symlink(link_status)) {
        detail = kErrorInvalidPath;
        return std::nullopt;
    }
    if (error == std::errc::no_such_file_or_directory) {
        error.clear();
    }
    const auto candidate =
        std::filesystem::weakly_canonical(lexical, error);
    if (error) {
        detail = error_from_code(error);
        return std::nullopt;
    }
    if (!contains(*save_root_, candidate)) {
        detail = kErrorInvalidPath;
        return std::nullopt;
    }
    detail = kErrorOk;
    return candidate;
}

std::optional<CafeFilesystem::ResolvedPath> CafeFilesystem::resolve_save(
    uint8_t slot, std::string_view guest_path, bool allow_missing,
    int32_t& detail) const noexcept {
    if (!save_initialized_) {
        detail = save_root_ ? kErrorNotInit : kErrorNotFound;
        return std::nullopt;
    }
    try {
        const auto root = save_slot(slot, detail);
        if (!root) {
            return std::nullopt;
        }
        std::error_code error;
        if (!std::filesystem::is_directory(*root, error)) {
            detail = error ? error_from_code(error) : kErrorNotFound;
            return std::nullopt;
        }
        const auto path = resolve_under(*root, guest_path, allow_missing,
                                        detail);
        if (!path) {
            return std::nullopt;
        }
        return ResolvedPath{*path, true};
    } catch (...) {
        detail = kErrorInvalidPath;
        return std::nullopt;
    }
}

std::optional<std::string> CafeFilesystem::guest_string(
    uint32_t address, uint32_t limit, uint32_t pc) const {
    if (address == 0) {
        return std::nullopt;
    }
    std::string value;
    value.reserve(limit);
    for (uint32_t index = 0; index < limit; ++index) {
        const auto byte = memory_.read8(address + index, pc);
        if (byte == 0) {
            return value;
        }
        value.push_back(static_cast<char>(byte));
    }
    return std::nullopt;
}

void CafeFilesystem::write_stat(uint32_t address,
                                const std::filesystem::path& path,
                                bool writable, uint32_t pc) {
    for (uint32_t offset = 0; offset < kStatSize; ++offset) {
        abi::write_u8(memory_, address, offset, 0, pc);
    }
    std::error_code error;
    const auto link_status = std::filesystem::symlink_status(path, error);
    const bool link = !error && std::filesystem::is_symlink(link_status);
    const auto status = std::filesystem::status(path, error);
    const bool directory = !error && std::filesystem::is_directory(status);
    const bool file = !error && std::filesystem::is_regular_file(status);
    uint32_t flags = directory ? kStatDirectory : (file ? kStatFile : 0);
    if (link) {
        flags |= kStatLink;
    }
    uint32_t size{};
    if (file) {
        const auto host_size = std::filesystem::file_size(path, error);
        if (!error) {
            size = static_cast<uint32_t>(std::min<uintmax_t>(
                host_size, std::numeric_limits<uint32_t>::max()));
        }
    }
    abi::write_u32(memory_, address, 0x00, flags, pc);
    abi::write_u32(memory_, address, 0x04,
                   directory ? (writable ? 0777U : 0555U)
                             : (writable ? 0666U : 0444U),
                   pc);
    abi::write_u32(memory_, address, 0x10, size, pc);
    abi::write_u32(memory_, address, 0x14, size, pc);
}

void CafeFilesystem::write_stat(uint32_t address,
                                const DirectoryEntry& entry, uint32_t pc) {
    for (uint32_t offset = 0; offset < kStatSize; ++offset) {
        abi::write_u8(memory_, address, offset, 0, pc);
    }
    abi::write_u32(memory_, address, 0x00, entry.flags, pc);
    abi::write_u32(memory_, address, 0x04, entry.mode, pc);
    abi::write_u32(memory_, address, 0x10, entry.size, pc);
    abi::write_u32(memory_, address, 0x14, entry.size, pc);
}

void CafeFilesystem::write_directory_entry(uint32_t address,
                                           const DirectoryEntry& entry,
                                           uint32_t pc) {
    for (uint32_t offset = 0; offset < kDirectoryEntrySize; ++offset) {
        abi::write_u8(memory_, address, offset, 0, pc);
    }
    write_stat(address, entry, pc);
    const auto length = std::min<size_t>(entry.name.size(), 255);
    for (size_t index = 0; index < length; ++index) {
        abi::write_u8(memory_, address, 0x64 + static_cast<uint32_t>(index),
                      static_cast<uint8_t>(entry.name[index]), pc);
    }
}

std::optional<std::vector<CafeFilesystem::DirectoryEntry>>
CafeFilesystem::snapshot_directory(const std::filesystem::path& path,
                                   bool writable, int32_t& detail) const {
    std::error_code error;
    if (!std::filesystem::is_directory(path, error)) {
        detail = error ? error_from_code(error) : kErrorNotDir;
        return std::nullopt;
    }
    std::vector<DirectoryEntry> entries;
    std::filesystem::directory_iterator iterator(path, error);
    const std::filesystem::directory_iterator end;
    while (!error && iterator != end) {
        const auto entry_path = iterator->path();
        const auto link_status = iterator->symlink_status(error);
        if (error) {
            break;
        }
        const bool link = std::filesystem::is_symlink(link_status);
        const auto status = iterator->status(error);
        if (error) {
            break;
        }
        if (link && std::filesystem::is_directory(status)) {
            iterator.increment(error);
            continue;
        }
        const auto canonical = std::filesystem::weakly_canonical(entry_path,
                                                                  error);
        const auto& namespace_root = writable ? *save_root_ : content_root_;
        if (error || !contains(namespace_root, canonical)) {
            error.clear();
            iterator.increment(error);
            continue;
        }
        DirectoryEntry entry;
        entry.name = entry_path.filename().string();
        if (entry.name.size() > 255) {
            entry.name.resize(255);
        }
        if (std::filesystem::is_directory(status)) {
            entry.flags = kStatDirectory;
            entry.mode = writable ? 0777U : 0555U;
        } else if (std::filesystem::is_regular_file(status)) {
            entry.flags = kStatFile;
            entry.mode = writable ? 0666U : 0444U;
            const auto host_size = iterator->file_size(error);
            if (error) {
                break;
            }
            entry.size = static_cast<uint32_t>(std::min<uintmax_t>(
                host_size, std::numeric_limits<uint32_t>::max()));
        } else {
            iterator.increment(error);
            continue;
        }
        if (link) {
            entry.flags |= kStatLink;
        }
        entries.push_back(std::move(entry));
        iterator.increment(error);
    }
    if (error) {
        detail = error_from_code(error);
        return std::nullopt;
    }
    std::sort(entries.begin(), entries.end(),
              [](const auto& left, const auto& right) {
                  return left.name < right.name;
              });
    detail = kErrorOk;
    return entries;
}

int32_t CafeFilesystem::finish(uint32_t client, int32_t detail,
                               uint32_t error_mask) {
    if (clients_.contains(client)) {
        last_errors_[client] = detail;
    }
    if (detail == kErrorOk) {
        return kStatusOk;
    }
    if (detail == kErrorEndDir) {
        return kStatusEnd;
    }
    if (detail == kErrorEndFile) {
        return kStatusOk;
    }
    struct Mapping {
        int32_t detail;
        uint32_t flag;
        int32_t status;
    };
    constexpr std::array mappings{
        Mapping{kErrorAlreadyOpen, kFlagAlreadyOpen, kStatusAlreadyOpen},
        Mapping{kErrorExists, kFlagExists, kStatusExists},
        Mapping{kErrorNotFound, kFlagNotFound, kStatusNotFound},
        Mapping{kErrorNotFile, kFlagNotFile, kStatusNotFile},
        Mapping{kErrorNotDir, kFlagNotDir, kStatusNotDir},
        Mapping{kErrorAccess, kFlagAccess, kStatusAccess},
        Mapping{kErrorPermission, kFlagPermission, kStatusPermission},
        Mapping{kErrorTooBig, kFlagTooBig, kStatusTooBig},
        Mapping{kErrorFull, kFlagFull, kStatusFull},
        Mapping{kErrorUnsupported, kFlagUnsupported, kStatusUnsupported},
    };
    const auto mapping = std::find_if(
        mappings.begin(), mappings.end(),
        [detail](const auto& candidate) { return candidate.detail == detail; });
    if (mapping != mappings.end() && (error_mask & mapping->flag) != 0) {
        return mapping->status;
    }
    return kStatusFatal;
}

int32_t CafeFilesystem::success(uint32_t client, int32_t result) {
    if (clients_.contains(client)) {
        last_errors_[client] = kErrorOk;
    }
    return result;
}

std::optional<int32_t> CafeFilesystem::command_failure(
    uint32_t client, uint32_t command, uint32_t error_mask) {
    if (!fs_initialized_) {
        return finish(client, kErrorNotInit, error_mask);
    }
    if (client == 0 || !clients_.contains(client)) {
        return kStatusFatal;
    }
    if (command == 0 || !commands_.contains(command)) {
        return finish(client, kErrorInvalidParam, error_mask);
    }
    return std::nullopt;
}

uint32_t CafeFilesystem::allocate_handle() {
    if (next_handle_ == 0) {
        return 0;
    }
    return next_handle_++;
}

void CafeFilesystem::close_client_handles(uint32_t client) {
    std::erase_if(files_, [client](const auto& item) {
        return item.second.client == client;
    });
    std::erase_if(directories_, [client](const auto& item) {
        return item.second.client == client;
    });
}

int32_t CafeFilesystem::open_file(
    uint32_t client, uint32_t command, std::string_view guest_path,
    std::string_view mode, uint32_t output_handle, uint32_t error_mask,
    bool save, uint8_t slot, uint32_t pc) {
    if (const auto failure = command_failure(client, command, error_mask)) {
        return *failure;
    }
    if (output_handle == 0) {
        return finish(client, kErrorInvalidBuffer, error_mask);
    }
    const auto parsed = parse_mode(mode);
    if (!parsed) {
        return finish(client, kErrorInvalidParam, error_mask);
    }
    if (!save && parsed->write) {
        return finish(client, kErrorPermission, error_mask);
    }
    int32_t detail{};
    const auto resolved = save
                              ? resolve_save(slot, guest_path, parsed->create,
                                             detail)
                              : resolve_content(guest_path, false, detail);
    if (!resolved) {
        return finish(client, detail, error_mask);
    }
    std::error_code error;
    const auto status = std::filesystem::status(resolved->path, error);
    if (error && error != std::errc::no_such_file_or_directory) {
        return finish(client, error_from_code(error), error_mask);
    }
    if (!error && std::filesystem::is_directory(status)) {
        return finish(client, kErrorNotFile, error_mask);
    }
    if (!parsed->create && (error || !std::filesystem::exists(status))) {
        return finish(client, kErrorNotFound, error_mask);
    }
    if (parsed->create) {
        const auto parent = resolved->path.parent_path();
        if (!std::filesystem::is_directory(parent, error)) {
            return finish(client, error ? error_from_code(error) : kErrorNotDir,
                          error_mask);
        }
        const auto canonical_parent = std::filesystem::weakly_canonical(
            parent, error);
        const auto& root = save ? *save_root_ : content_root_;
        if (error || !contains(root, canonical_parent)) {
            return finish(client, kErrorInvalidPath, error_mask);
        }
    }
    abi::write_u32(memory_, output_handle, 0, 0, pc);

    auto flags = std::ios::binary;
    if (parsed->read) {
        flags |= std::ios::in;
    }
    if (parsed->write) {
        flags |= std::ios::out;
    }
    if (parsed->truncate) {
        flags |= std::ios::trunc;
    }
    if (parsed->append) {
        flags |= std::ios::app;
    }
    File file;
    file.client = client;
    file.path = resolved->path;
    file.read = parsed->read;
    file.write = parsed->write;
    file.append = parsed->append;
    file.namespace_writable = resolved->writable;
    file.stream.open(file.path, flags);
    if (!file.stream.is_open()) {
        return finish(client, errno == EACCES ? kErrorPermission
                                              : kErrorNotFound,
                      error_mask);
    }
    if (parsed->append && !parsed->read) {
        file.stream.seekp(0, std::ios::end);
        const auto end = file.stream.tellp();
        file.position = end < 0 ? 0 : static_cast<uint64_t>(end);
    }
    const auto handle = allocate_handle();
    if (handle == 0) {
        return finish(client, kErrorOutOfResources, error_mask);
    }
    abi::write_u32(memory_, output_handle, 0, handle, pc);
    files_.emplace(handle, std::move(file));
    return success(client);
}

int32_t CafeFilesystem::open_directory(
    uint32_t client, uint32_t command, std::string_view guest_path,
    uint32_t output_handle, uint32_t error_mask, bool save, uint8_t slot,
    uint32_t pc) {
    if (const auto failure = command_failure(client, command, error_mask)) {
        return *failure;
    }
    if (output_handle == 0) {
        return finish(client, kErrorInvalidBuffer, error_mask);
    }
    int32_t detail{};
    const auto resolved = save ? resolve_save(slot, guest_path, false, detail)
                               : resolve_content(guest_path, false, detail);
    if (!resolved) {
        return finish(client, detail, error_mask);
    }
    auto entries = snapshot_directory(resolved->path, resolved->writable,
                                      detail);
    if (!entries) {
        return finish(client, detail, error_mask);
    }
    const auto handle = allocate_handle();
    if (handle == 0) {
        return finish(client, kErrorOutOfResources, error_mask);
    }
    abi::write_u32(memory_, output_handle, 0, handle, pc);
    directories_.emplace(
        handle, Directory{client, std::move(*entries), 0});
    return success(client);
}

void CafeFilesystem::register_handlers(CafeRuntime& runtime) {
    auto bind = [this, &runtime](std::string module, std::string symbol,
                                 auto operation) {
        runtime.register_handler(
            std::move(module), std::move(symbol),
            [this, operation = std::move(operation)](CPUContext& cpu,
                                                     GuestMemory&) {
                try {
                    cpu.gpr[3] = as_guest(operation(cpu));
                } catch (const GuestFault&) {
                    throw;
                } catch (...) {
                    const auto client = cpu.gpr[3];
                    if (clients_.contains(client)) {
                        last_errors_[client] = kErrorAccess;
                    }
                    cpu.gpr[3] = as_guest(kStatusFatal);
                }
                return HleAction::return_to_lr;
            });
    };

    bind("coreinit", "FSInit", [this](CPUContext&) {
        fs_initialized_ = true;
        return kStatusOk;
    });
    bind("coreinit", "FSShutdown", [this](CPUContext&) {
        files_.clear();
        directories_.clear();
        clients_.clear();
        commands_.clear();
        command_priorities_.clear();
        last_errors_.clear();
        notifications_.clear();
        fs_initialized_ = false;
        save_initialized_ = false;
        return kStatusOk;
    });
    bind("coreinit", "FSAddClient", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[4];
        if (!fs_initialized_) {
            return kStatusFatal;
        }
        if (client == 0) {
            return kStatusFatal;
        }
        if (clients_.contains(client)) {
            return finish(client, kErrorAlreadyOpen, mask);
        }
        clients_.insert(client);
        last_errors_[client] = kErrorOk;
        return kStatusOk;
    });
    bind("coreinit", "FSDelClient", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        if (!fs_initialized_ || !clients_.contains(client)) {
            return kStatusFatal;
        }
        close_client_handles(client);
        clients_.erase(client);
        last_errors_.erase(client);
        notifications_.erase(client);
        return kStatusOk;
    });
    bind("coreinit", "FSInitCmdBlock", [this](CPUContext& cpu) {
        if (fs_initialized_ && cpu.gpr[3] != 0) {
            commands_.insert(cpu.gpr[3]);
            command_priorities_[cpu.gpr[3]] = 16;
        }
        return kStatusOk;
    });
    bind("coreinit", "FSSetCmdPriority", [this](CPUContext& cpu) {
        if (!fs_initialized_ || !commands_.contains(cpu.gpr[3])) {
            return kStatusFatal;
        }
        command_priorities_[cpu.gpr[3]] = cpu.gpr[4];
        return kStatusOk;
    });
    bind("coreinit", "FSSetStateChangeNotification",
         [this](CPUContext& cpu) {
             const auto client = cpu.gpr[3];
             const auto address = cpu.gpr[4];
             if (fs_initialized_ && clients_.contains(client) && address != 0) {
                 notifications_[client] = {
                     abi::read_u32(memory_, address, 0, cpu.pc),
                     abi::read_u32(memory_, address, 4, cpu.pc),
                     abi::read_u32(memory_, address, 8, cpu.pc)};
             }
             return kStatusOk;
         });
    bind("coreinit", "FSGetCwd", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[7];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        constexpr std::string_view cwd = "/vol/content";
        if (cpu.gpr[5] == 0 || cpu.gpr[6] <= cwd.size()) {
            return finish(client, kErrorInvalidBuffer, mask);
        }
        for (size_t index = 0; index < cwd.size(); ++index) {
            memory_.write8(cpu.gpr[5] + static_cast<uint32_t>(index),
                           static_cast<uint8_t>(cwd[index]), cpu.pc);
        }
        memory_.write8(cpu.gpr[5] + static_cast<uint32_t>(cwd.size()), 0,
                       cpu.pc);
        return success(client);
    });
    bind("coreinit", "FSGetLastError", [this](CPUContext& cpu) {
        if (!fs_initialized_) {
            return kErrorNotInit;
        }
        const auto error = last_errors_.find(cpu.gpr[3]);
        return error == last_errors_.end() ? kErrorInvalidClient
                                           : error->second;
    });
    bind("coreinit", "FSGetLastErrorCodeForViewer",
         [this](CPUContext& cpu) {
             if (!fs_initialized_) {
                 return kErrorNotInit;
             }
             const auto error = last_errors_.find(cpu.gpr[3]);
             return error == last_errors_.end() ? kErrorInvalidClient
                                                : error->second;
         });
    bind("coreinit", "FSGetVolumeState", [this](CPUContext& cpu) {
        return fs_initialized_ && clients_.contains(cpu.gpr[3]) ? 1 : 11;
    });
    bind("coreinit", "FSGetStat", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[7];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        const auto path = guest_string(cpu.gpr[5], kPathLimit, cpu.pc);
        if (!path || cpu.gpr[6] == 0) {
            return finish(client, path ? kErrorInvalidBuffer
                                       : kErrorInvalidPath,
                          mask);
        }
        int32_t detail{};
        const auto resolved = resolve_content(*path, false, detail);
        if (!resolved) {
            return finish(client, detail, mask);
        }
        write_stat(cpu.gpr[6], resolved->path, false, cpu.pc);
        return success(client);
    });
    bind("coreinit", "FSOpenFile", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[8];
        const auto path = guest_string(cpu.gpr[5], kPathLimit, cpu.pc);
        const auto mode = guest_string(cpu.gpr[6], kModeLimit, cpu.pc);
        if (!path || !mode) {
            return finish(client, kErrorInvalidPath, mask);
        }
        const auto result =
            open_file(client, cpu.gpr[4], *path, *mode, cpu.gpr[7], mask,
                      false, 0, cpu.pc);
        if (std::getenv("NWIIU_FS_TRACE") != nullptr) {
            std::fprintf(stderr, "FSOPEN result=%d path=%s\n", result,
                         path->c_str());
        }
        return result;
    });
    bind("coreinit", "FSCloseFile", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[6];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        const auto file = files_.find(cpu.gpr[5]);
        if (file == files_.end() || file->second.client != client) {
            return finish(client, kErrorInvalidFile, mask);
        }
        files_.erase(file);
        return success(client);
    });
    bind("coreinit", "FSGetStatFile", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[7];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        const auto file = files_.find(cpu.gpr[5]);
        if (file == files_.end() || file->second.client != client) {
            return finish(client, kErrorInvalidFile, mask);
        }
        if (cpu.gpr[6] == 0) {
            return finish(client, kErrorInvalidBuffer, mask);
        }
        write_stat(cpu.gpr[6], file->second.path,
                   file->second.namespace_writable, cpu.pc);
        return success(client);
    });
    bind("coreinit", "FSReadFile", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[10];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        const auto found = files_.find(cpu.gpr[8]);
        if (found == files_.end() || found->second.client != client) {
            return finish(client, kErrorInvalidFile, mask);
        }
        auto& file = found->second;
        if (!file.read) {
            return finish(client, kErrorPermission, mask);
        }
        const uint64_t total = static_cast<uint64_t>(cpu.gpr[6]) * cpu.gpr[7];
        if (cpu.gpr[6] == 0 || total == 0) {
            return success(client);
        }
        if (cpu.gpr[5] == 0 || total > std::numeric_limits<uint32_t>::max()) {
            return finish(client, kErrorInvalidBuffer, mask);
        }
        file.stream.clear();
        file.stream.seekg(static_cast<std::streamoff>(file.position),
                          std::ios::beg);
        if (file.stream.fail()) {
            return finish(client, kErrorOutOfRange, mask);
        }
        std::array<char, 4096> bytes{};
        uint64_t transferred{};
        while (transferred < total) {
            const auto amount = static_cast<std::streamsize>(
                std::min<uint64_t>(bytes.size(), total - transferred));
            file.stream.read(bytes.data(), amount);
            const auto read = file.stream.gcount();
            for (std::streamsize index = 0; index < read; ++index) {
                memory_.write8(cpu.gpr[5] +
                                   static_cast<uint32_t>(transferred + index),
                               static_cast<uint8_t>(bytes[index]), cpu.pc);
            }
            transferred += static_cast<uint64_t>(read);
            if (read != amount) {
                break;
            }
        }
        file.position += transferred;
        file.stream.clear();
        if (transferred == 0) {
            finish(client, kErrorEndFile, mask);
            return kStatusOk;
        }
        return success(client,
                       static_cast<int32_t>(transferred / cpu.gpr[6]));
    });
    bind("coreinit", "FSWriteFile", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[10];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        const auto found = files_.find(cpu.gpr[8]);
        if (found == files_.end() || found->second.client != client) {
            return finish(client, kErrorInvalidFile, mask);
        }
        auto& file = found->second;
        if (!file.write || !file.namespace_writable) {
            return finish(client, kErrorPermission, mask);
        }
        const uint64_t total = static_cast<uint64_t>(cpu.gpr[6]) * cpu.gpr[7];
        if (cpu.gpr[6] == 0 || total == 0) {
            return success(client);
        }
        if (cpu.gpr[5] == 0 ||
            total > std::numeric_limits<uint32_t>::max() ||
            total - 1 >
                std::numeric_limits<uint32_t>::max() - cpu.gpr[5]) {
            return finish(client, kErrorInvalidBuffer, mask);
        }
        for (uint64_t offset = 0; offset < total; ++offset) {
            memory_.read8(cpu.gpr[5] + static_cast<uint32_t>(offset), cpu.pc);
        }
        file.stream.clear();
        if (file.append) {
            file.stream.seekp(0, std::ios::end);
        } else {
            file.stream.seekp(static_cast<std::streamoff>(file.position),
                              std::ios::beg);
        }
        if (file.stream.fail()) {
            return finish(client, kErrorOutOfRange, mask);
        }
        std::array<char, 4096> bytes{};
        uint64_t transferred{};
        while (transferred < total) {
            const auto amount = static_cast<size_t>(
                std::min<uint64_t>(bytes.size(), total - transferred));
            for (size_t index = 0; index < amount; ++index) {
                bytes[index] = static_cast<char>(memory_.read8(
                    cpu.gpr[5] + static_cast<uint32_t>(transferred + index),
                    cpu.pc));
            }
            file.stream.write(bytes.data(),
                              static_cast<std::streamsize>(amount));
            if (file.stream.fail()) {
                return finish(client, kErrorAccess, mask);
            }
            transferred += amount;
        }
        errno = 0;
        file.stream.flush();
        if (file.stream.fail()) {
            return finish(client, errno == ENOSPC ? kErrorFull : kErrorAccess,
                          mask);
        }
        const auto position = file.stream.tellp();
        file.position = position < 0 ? file.position + transferred
                                     : static_cast<uint64_t>(position);
        return success(client,
                       static_cast<int32_t>(transferred / cpu.gpr[6]));
    });
    bind("coreinit", "FSSetPosFile", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[7];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        const auto file = files_.find(cpu.gpr[5]);
        if (file == files_.end() || file->second.client != client) {
            return finish(client, kErrorInvalidFile, mask);
        }
        file->second.position = cpu.gpr[6];
        file->second.stream.clear();
        return success(client);
    });
    bind("coreinit", "FSOpenDir", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[7];
        const auto path = guest_string(cpu.gpr[5], kPathLimit, cpu.pc);
        if (!path) {
            return finish(client, kErrorInvalidPath, mask);
        }
        return open_directory(client, cpu.gpr[4], *path, cpu.gpr[6], mask,
                              false, 0, cpu.pc);
    });
    bind("coreinit", "FSReadDir", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[7];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        const auto directory = directories_.find(cpu.gpr[5]);
        if (directory == directories_.end() ||
            directory->second.client != client) {
            return finish(client, kErrorInvalidDir, mask);
        }
        if (cpu.gpr[6] == 0) {
            return finish(client, kErrorInvalidBuffer, mask);
        }
        if (directory->second.position >= directory->second.entries.size()) {
            return finish(client, kErrorEndDir, mask);
        }
        write_directory_entry(
            cpu.gpr[6],
            directory->second.entries[directory->second.position++], cpu.pc);
        return success(client);
    });
    bind("coreinit", "FSCloseDir", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[6];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        const auto directory = directories_.find(cpu.gpr[5]);
        if (directory == directories_.end() ||
            directory->second.client != client) {
            return finish(client, kErrorInvalidDir, mask);
        }
        directories_.erase(directory);
        return success(client);
    });

    bind("nn_save", "SAVEInit", [this](CPUContext&) {
        if (!save_root_) {
            return kStatusNotFound;
        }
        save_initialized_ = true;
        return kStatusOk;
    });
    bind("nn_save", "SAVEShutdown", [this](CPUContext&) {
        save_initialized_ = false;
        return kStatusOk;
    });
    bind("nn_save", "SAVEInitSaveDir", [this](CPUContext& cpu) {
        if (!save_root_) {
            return kStatusNotFound;
        }
        if (!save_initialized_) {
            return kStatusNotFound;
        }
        std::error_code error;
        if (std::filesystem::exists(*save_root_, error) &&
            !std::filesystem::is_directory(*save_root_, error)) {
            return kStatusFull;
        }
        std::filesystem::create_directories(*save_root_, error);
        if (error) {
            return kStatusFull;
        }
        int32_t detail{};
        const auto slot = save_slot(static_cast<uint8_t>(cpu.gpr[3]), detail);
        if (!slot) {
            return kStatusFull;
        }
        std::filesystem::create_directories(*slot, error);
        if (error) {
            return kStatusFull;
        }
        const auto checked = std::filesystem::weakly_canonical(*slot, error);
        if (error || !contains(*save_root_, checked)) {
            return kStatusFull;
        }
        return kStatusOk;
    });
    bind("nn_save", "SAVEFlushQuota", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[6];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        int32_t detail{};
        const auto root = resolve_save(static_cast<uint8_t>(cpu.gpr[5]), "",
                                       false, detail);
        return root ? success(client) : finish(client, detail, mask);
    });
    bind("nn_save", "SAVEGetStat", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[7];
        if (const auto failure = command_failure(client, cpu.gpr[4], mask)) {
            return *failure;
        }
        if (cpu.gpr[6] == 0) {
            return finish(client, kErrorInvalidBuffer, mask);
        }
        int32_t detail{};
        const auto root = resolve_save(static_cast<uint8_t>(cpu.gpr[5]), "",
                                       false, detail);
        if (!root) {
            return finish(client, detail, mask);
        }
        write_stat(cpu.gpr[6], root->path, true, cpu.pc);
        return success(client);
    });
    bind("nn_save", "SAVEOpenFile", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[9];
        const auto path = guest_string(cpu.gpr[6], kPathLimit, cpu.pc);
        const auto mode = guest_string(cpu.gpr[7], kModeLimit, cpu.pc);
        if (!path || !mode) {
            return finish(client, kErrorInvalidPath, mask);
        }
        return open_file(client, cpu.gpr[4], *path, *mode, cpu.gpr[8], mask,
                         true, static_cast<uint8_t>(cpu.gpr[5]), cpu.pc);
    });
    bind("nn_save", "SAVEOpenDir", [this](CPUContext& cpu) {
        const auto client = cpu.gpr[3];
        const auto mask = cpu.gpr[8];
        const auto path = guest_string(cpu.gpr[6], kPathLimit, cpu.pc);
        if (!path) {
            return finish(client, kErrorInvalidPath, mask);
        }
        return open_directory(client, cpu.gpr[4], *path, cpu.gpr[7], mask,
                              true, static_cast<uint8_t>(cpu.gpr[5]), cpu.pc);
    });
}
} // namespace nwii::runtime
