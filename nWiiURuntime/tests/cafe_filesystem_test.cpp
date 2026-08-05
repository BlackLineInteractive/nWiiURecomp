#include "runtime/cafe_filesystem.h"
#include "runtime/cafe_runtime.h"
#include "runtime/executor.h"
#include "test_support.h"

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::CafeFilesystem;
using nwii::runtime::CafeRuntime;
using nwii::runtime::ExecutionImage;
using nwii::runtime::Executor;
using nwii::runtime::StopCategory;

constexpr uint32_t kReturn = 0x02000000;
constexpr uint32_t kImportBase = 0xC0007000;
constexpr uint32_t kData = 0x10000000;
constexpr uint32_t kClientA = kData;
constexpr uint32_t kClientB = kData + 0x2000;
constexpr uint32_t kCmdA = kData + 0x4000;
constexpr uint32_t kCmdB = kData + 0x5000;
constexpr uint32_t kPath = kData + 0x7000;
constexpr uint32_t kMode = kData + 0x7400;
constexpr uint32_t kHandle = kData + 0x7500;
constexpr uint32_t kBuffer = kData + 0x7600;
constexpr uint32_t kStat = kData + 0x7800;
constexpr uint32_t kEntry = kData + 0x7900;
constexpr uint32_t kState = kData + 0x7B00;
constexpr uint32_t kAllErrors = 0xFFFFFFFF;

constexpr int32_t kOk = 0;
constexpr int32_t kEnd = -2;
constexpr int32_t kAlreadyOpen = -4;
constexpr int32_t kNotFound = -6;
constexpr int32_t kStorageFull = -12;
constexpr int32_t kPermission = -10;
constexpr int32_t kFatal = -0x400;
constexpr int32_t kErrorNotInit = -0x30001;
constexpr int32_t kErrorEndDir = -0x30004;
constexpr int32_t kErrorEndFile = -0x30005;
constexpr int32_t kErrorNotFound = -0x30017;
constexpr int32_t kErrorPermission = -0x3001A;
constexpr int32_t kErrorInvalidParam = -0x30021;
constexpr int32_t kErrorInvalidPath = -0x30022;
constexpr int32_t kErrorInvalidFile = -0x30026;
constexpr int32_t kErrorInvalidDir = -0x30027;

constexpr std::array<std::pair<std::string_view, std::string_view>, 28>
    kImports{{
        {"coreinit", "FSAddClient"},
        {"coreinit", "FSCloseDir"},
        {"coreinit", "FSCloseFile"},
        {"coreinit", "FSDelClient"},
        {"coreinit", "FSGetCwd"},
        {"coreinit", "FSGetLastError"},
        {"coreinit", "FSGetLastErrorCodeForViewer"},
        {"coreinit", "FSGetStat"},
        {"coreinit", "FSGetStatFile"},
        {"coreinit", "FSGetVolumeState"},
        {"coreinit", "FSInit"},
        {"coreinit", "FSInitCmdBlock"},
        {"coreinit", "FSOpenDir"},
        {"coreinit", "FSOpenFile"},
        {"coreinit", "FSReadDir"},
        {"coreinit", "FSReadFile"},
        {"coreinit", "FSSetCmdPriority"},
        {"coreinit", "FSSetPosFile"},
        {"coreinit", "FSSetStateChangeNotification"},
        {"coreinit", "FSShutdown"},
        {"coreinit", "FSWriteFile"},
        {"nn_save", "SAVEFlushQuota"},
        {"nn_save", "SAVEGetStat"},
        {"nn_save", "SAVEInit"},
        {"nn_save", "SAVEInitSaveDir"},
        {"nn_save", "SAVEOpenDir"},
        {"nn_save", "SAVEOpenFile"},
        {"nn_save", "SAVEShutdown"},
    }};

struct TempTree {
    TempTree() {
        const auto nonce = std::chrono::steady_clock::now()
                               .time_since_epoch()
                               .count();
        root = std::filesystem::temp_directory_path() /
               ("nwiiu-cafe-fs-" + std::to_string(nonce));
        title = root / "title";
        content = title / "content";
        save = root / "save";
        outside = root / "outside";
        std::filesystem::create_directories(content / "Common");
        std::filesystem::create_directories(outside / "dir");
        write(content / "Common" / "test.bin", "abcdef");
        write(content / "Common" / "z.bin", "z");
        write(content / "Common" / "a.bin", "a");
        write(outside / "secret.bin", "secret");
    }

    ~TempTree() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    static void write(const std::filesystem::path& path,
                      std::string_view bytes) {
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    static std::string read(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>{input},
                std::istreambuf_iterator<char>{}};
    }

    std::filesystem::path root;
    std::filesystem::path title;
    std::filesystem::path content;
    std::filesystem::path save;
    std::filesystem::path outside;
};

ExecutionImage make_image() {
    ExecutionImage image;
    constexpr std::array<uint8_t, 4> branch{0x48, 0, 0, 0};
    image.memory.map(kReturn, branch.size(), {true, false, true}, branch);
    image.memory.map(kData, 0x10000, {true, true, false});
    for (size_t index = 0; index < kImports.size(); ++index) {
        image.imports.emplace(
            kImportBase + static_cast<uint32_t>(index * 4),
            nwii::runtime::ImportTarget{std::string{kImports[index].first},
                                        std::string{kImports[index].second}});
    }
    return image;
}

struct Fixture {
    Fixture(const std::filesystem::path& title_root,
            std::optional<std::filesystem::path> save_root)
        : image(make_image()), filesystem(image, title_root, std::move(save_root)),
          runtime(image), executor(image) {
        filesystem.register_handlers(runtime);
        runtime.register_imports(executor);
    }

    uint32_t address(std::string_view symbol) const {
        for (size_t index = 0; index < kImports.size(); ++index) {
            if (kImports[index].second == symbol) {
                return kImportBase + static_cast<uint32_t>(index * 4);
            }
        }
        throw std::runtime_error("unknown test import");
    }

    nwii::runtime::ExecutionStop
    execute(CPUContext& cpu, std::string_view symbol,
            std::initializer_list<uint32_t> arguments = {}) {
        cpu = {};
        cpu.pc = address(symbol);
        cpu.lr = kReturn;
        size_t index = 3;
        for (const auto argument : arguments) {
            cpu.gpr[index++] = argument;
        }
        return executor.run(cpu, 1);
    }

    int32_t call(std::string_view symbol,
                 std::initializer_list<uint32_t> arguments = {}) {
        CPUContext cpu;
        const auto stop = execute(cpu, symbol, arguments);
        test::require(stop.category == StopCategory::instruction_budget,
                      std::string{symbol} + " is bound and returns");
        called.emplace(symbol);
        return static_cast<int32_t>(cpu.gpr[3]);
    }

    void string(uint32_t address, std::string_view value) {
        for (size_t index = 0; index < value.size(); ++index) {
            image.memory.write8(address + static_cast<uint32_t>(index),
                                static_cast<uint8_t>(value[index]), 0);
        }
        image.memory.write8(address + static_cast<uint32_t>(value.size()), 0,
                            0);
    }

    std::string guest_string(uint32_t address) const {
        std::string result;
        while (const auto byte = image.memory.read8(address++, 0)) {
            result.push_back(static_cast<char>(byte));
        }
        return result;
    }

    uint32_t handle() const { return image.memory.read32(kHandle, 0); }

    void initialize(uint32_t client = kClientA, uint32_t cmd = kCmdA) {
        call("FSInit");
        call("FSInitCmdBlock", {cmd});
        test::require(call("FSAddClient", {client, kAllErrors}) == kOk,
                      "filesystem client initialized");
    }

    ExecutionImage image;
    CafeFilesystem filesystem;
    CafeRuntime runtime;
    Executor executor;
    std::set<std::string_view> called;
};

void require_stat(const Fixture& fixture, uint32_t address, uint32_t flags,
                  uint32_t mode, uint32_t size, std::string_view message) {
    const auto& memory = fixture.image.memory;
    test::require(memory.read32(address + 0x00, 0) == flags &&
                      memory.read32(address + 0x04, 0) == mode &&
                      memory.read32(address + 0x10, 0) == size &&
                      memory.read32(address + 0x14, 0) == size &&
                      memory.read64(address + 0x18, 0) == 0 &&
                      memory.read32(address + 0x20, 0) == 0 &&
                      memory.read64(address + 0x24, 0) == 0 &&
                      memory.read64(address + 0x2C, 0) == 0 &&
                      memory.read32(address + 0x60, 0) == 0,
                  message);
}

void test_exact_imports_and_end_to_end_io() {
    TempTree tree;
    Fixture fixture(tree.title, tree.save);
    fixture.initialize();

    test::require(fixture.call("FSSetCmdPriority", {kCmdA, 7}) == kOk,
                  "command priority accepted");
    fixture.image.memory.write32(kState + 0, 0x11111111, 0);
    fixture.image.memory.write32(kState + 4, 0x22222222, 0);
    fixture.image.memory.write32(kState + 8, 0x33333333, 0);
    fixture.call("FSSetStateChangeNotification", {kClientA, kState});
    test::require(fixture.call("FSGetVolumeState", {kClientA}) == 1,
                  "initialized client sees ready volume");
    test::require(fixture.call("FSGetCwd", {kClientA, kCmdA, kBuffer, 32,
                                            kAllErrors}) == kOk &&
                      fixture.guest_string(kBuffer) == "/vol/content",
                  "content cwd returned");

    fixture.string(kPath, "/vol/content//Common/./test.bin");
    fixture.image.memory.write32(kStat + 0x60, 0xAAAAAAAA, 0);
    test::require(fixture.call("FSGetStat", {kClientA, kCmdA, kPath, kStat,
                                             kAllErrors}) == kOk,
                  "normalized content stat succeeds");
    require_stat(fixture, kStat, 0x01000000, 0444, 6,
                 "packed deterministic big-endian file stat");

    fixture.string(kMode, "rb");
    test::require(fixture.call("FSOpenFile", {kClientA, kCmdA, kPath, kMode,
                                              kHandle, kAllErrors}) == kOk,
                  "content file opens for read");
    const auto content_handle = fixture.handle();
    test::require(content_handle != 0, "nonzero file handle");
    test::require(fixture.call("FSGetStatFile", {kClientA, kCmdA,
                                                 content_handle, kStat,
                                                 kAllErrors}) == kOk,
                  "open file stat succeeds");
    test::require(fixture.call("FSReadFile", {kClientA, kCmdA, kBuffer, 1, 3,
                                              content_handle, 0,
                                              kAllErrors}) == 3 &&
                      fixture.image.memory.read8(kBuffer + 0, 0) == 'a' &&
                      fixture.image.memory.read8(kBuffer + 1, 0) == 'b' &&
                      fixture.image.memory.read8(kBuffer + 2, 0) == 'c',
                  "content read returns transferred count");
    test::require(fixture.call("FSSetPosFile", {kClientA, kCmdA,
                                                content_handle, 4,
                                                kAllErrors}) == kOk &&
                      fixture.call("FSReadFile", {kClientA, kCmdA, kBuffer,
                                                  1, 3, content_handle, 0,
                                                  kAllErrors}) == 2,
                  "absolute seek controls following read");
    test::require(fixture.call("FSReadFile", {kClientA, kCmdA, kBuffer, 1, 1,
                                              content_handle, 0,
                                              kAllErrors}) == 0 &&
                      fixture.call("FSGetLastError", {kClientA}) ==
                          kErrorEndFile,
                  "EOF returns zero and records detail");
    fixture.image.memory.write8(kBuffer, 'X', 0);
    test::require(fixture.call("FSWriteFile", {kClientA, kCmdA, kBuffer, 1,
                                               1, content_handle, 0,
                                               kAllErrors}) == kPermission &&
                      fixture.call("FSGetLastErrorCodeForViewer", {kClientA}) ==
                          kErrorPermission,
                  "content handle remains read-only");
    test::require(fixture.call("FSCloseFile", {kClientA, kCmdA,
                                               content_handle,
                                               kAllErrors}) == kOk,
                  "content file closes");

    fixture.string(kPath, "/vol/content/common/TEST.BIN");
    test::require(fixture.call("FSOpenFile", {kClientA, kCmdA, kPath, kMode,
                                              kHandle, kAllErrors}) == kOk,
                  "content lookup ignores host filesystem case");
    test::require(fixture.call("FSCloseFile", {kClientA, kCmdA,
                                               fixture.handle(),
                                               kAllErrors}) == kOk,
                  "case-insensitive content handle closes");

    fixture.string(kPath, "/vol/content/Common");
    test::require(fixture.call("FSOpenDir", {kClientA, kCmdA, kPath, kHandle,
                                             kAllErrors}) == kOk,
                  "content directory opens");
    const auto content_dir = fixture.handle();
    test::require(fixture.call("FSReadDir", {kClientA, kCmdA, content_dir,
                                             kEntry, kAllErrors}) == kOk &&
                      fixture.guest_string(kEntry + 0x64) == "a.bin",
                  "directory snapshot is sorted");
    test::require(fixture.call("FSCloseDir", {kClientA, kCmdA, content_dir,
                                              kAllErrors}) == kOk,
                  "content directory closes");

    test::require(fixture.call("SAVEInit") == kOk,
                  "save subsystem initializes with explicit root");
    test::require(fixture.call("SAVEInitSaveDir", {2}) == kOk,
                  "save slot directory initializes");
    test::require(fixture.call("SAVEFlushQuota", {kClientA, kCmdA, 2,
                                                  kAllErrors}) == kOk,
                  "save quota flush validates slot");
    test::require(fixture.call("SAVEGetStat", {kClientA, kCmdA, 2, kStat,
                                               kAllErrors}) == kOk,
                  "save slot stat succeeds");
    require_stat(fixture, kStat, 0x80000000, 0777, 0,
                 "packed deterministic save directory stat");

    fixture.string(kPath, "progress.bin");
    fixture.string(kMode, "w+");
    test::require(fixture.call("SAVEOpenFile", {kClientA, kCmdA, 2, kPath,
                                                kMode, kHandle,
                                                kAllErrors}) == kOk,
                  "save file opens read-write");
    const auto save_handle = fixture.handle();
    for (uint32_t index = 0; index < 4; ++index) {
        fixture.image.memory.write8(kBuffer + index,
                                    static_cast<uint8_t>('W' + index), 0);
    }
    test::require(fixture.call("FSWriteFile", {kClientA, kCmdA, kBuffer, 1,
                                               4, save_handle, 0,
                                               kAllErrors}) == 4 &&
                      fixture.call("FSSetPosFile", {kClientA, kCmdA,
                                                    save_handle, 0,
                                                    kAllErrors}) == kOk &&
                      fixture.call("FSReadFile", {kClientA, kCmdA, kBuffer,
                                                  1, 4, save_handle, 0,
                                                  kAllErrors}) == 4,
                  "save write/read uses generic file handle");
    test::require(fixture.call("FSCloseFile", {kClientA, kCmdA, save_handle,
                                               kAllErrors}) == kOk,
                  "save file closes through coreinit");

    fixture.string(kPath, "");
    test::require(fixture.call("SAVEOpenDir", {kClientA, kCmdA, 2, kPath,
                                               kHandle, kAllErrors}) == kOk,
                  "save root directory opens");
    const auto save_dir = fixture.handle();
    test::require(fixture.call("FSCloseDir", {kClientA, kCmdA, save_dir,
                                              kAllErrors}) == kOk,
                  "save directory closes through coreinit");
    fixture.call("SAVEShutdown");
    test::require(fixture.call("FSDelClient", {kClientA, kAllErrors}) == kOk,
                  "client deletion succeeds");
    fixture.call("FSShutdown");

    for (const auto& [module, symbol] : kImports) {
        (void)module;
        test::require(fixture.called.contains(symbol),
                      std::string{symbol} + " exact import exercised");
    }
}

void test_path_validation_containment_and_deterministic_directories() {
    TempTree tree;
    std::error_code error;
    std::filesystem::create_symlink(tree.outside / "secret.bin",
                                    tree.content / "escape.bin", error);
    test::require(!error, "outward file symlink fixture created");
    std::filesystem::create_directory_symlink(tree.outside / "dir",
                                               tree.content / "Common" /
                                                   "linked-dir",
                                               error);
    test::require(!error, "directory symlink fixture created");

    Fixture fixture(tree.title, tree.save);
    const auto normalized = fixture.filesystem.resolve_content_path(
        "/vol/content//Common/./test.bin");
    test::require(normalized &&
                      *normalized == std::filesystem::weakly_canonical(
                                         tree.content / "Common" / "test.bin"),
                  "resolver normalizes within canonical content root");
    const std::string nul_path{"/vol/content/Common/test.bin\0/escape", 43};
    test::require(!fixture.filesystem.resolve_content_path(nul_path),
                  "resolver seam rejects embedded NUL");
    test::require(!fixture.filesystem.resolve_content_path(
                      "/vol/content/Common/../Common/test.bin") &&
                      !fixture.filesystem.resolve_content_path("/tmp/test.bin") &&
                      !fixture.filesystem.resolve_content_path(
                          "/vol/contentish/Common/test.bin") &&
                      !fixture.filesystem.resolve_content_path(
                          "/vol/content/escape.bin"),
                  "dot-dot, host paths, unknown namespace, and escape rejected");

    fixture.initialize();
    const std::array<std::string_view, 4> invalid{
        "/vol/content/Common/../Common/test.bin", "/tmp/test.bin",
        "/vol/contentish/Common/test.bin", "/vol/content/escape.bin"};
    for (const auto path : invalid) {
        fixture.string(kPath, path);
        test::require(fixture.call("FSGetStat", {kClientA, kCmdA, kPath,
                                                 kStat, kAllErrors}) == kFatal &&
                          fixture.call("FSGetLastError", {kClientA}) ==
                              kErrorInvalidPath,
                      "invalid guest path translated without escape");
    }

    fixture.string(kPath, "/vol/content/Common");
    test::require(fixture.call("FSOpenDir", {kClientA, kCmdA, kPath, kHandle,
                                             kAllErrors}) == kOk,
                  "directory opens for snapshot test");
    const auto directory = fixture.handle();
    std::array<std::string, 3> expected{"a.bin", "test.bin", "z.bin"};
    for (const auto& name : expected) {
        for (uint32_t index = 0; index < 0x164; ++index) {
            fixture.image.memory.write8(kEntry + index, 0xAA, 0);
        }
        test::require(fixture.call("FSReadDir", {kClientA, kCmdA, directory,
                                                 kEntry, kAllErrors}) == kOk &&
                          fixture.guest_string(kEntry + 0x64) == name &&
                          fixture.image.memory.read8(kEntry + 0x163, 0) == 0,
                      "sorted snapshot entry and deterministic zero tail");
    }
    test::require(fixture.call("FSReadDir", {kClientA, kCmdA, directory,
                                             kEntry, kAllErrors}) == kEnd &&
                      fixture.call("FSGetLastError", {kClientA}) ==
                          kErrorEndDir,
                  "directory exhaustion returns END");
}

void test_typed_owned_monotonic_handles_and_client_errors() {
    TempTree tree;
    Fixture fixture(tree.title, tree.save);
    fixture.initialize();
    fixture.call("FSInitCmdBlock", {kCmdB});
    test::require(fixture.call("FSAddClient", {kClientB, kAllErrors}) == kOk,
                  "second client initialized");

    fixture.string(kPath, "/vol/content/Common/test.bin");
    fixture.string(kMode, "r");
    test::require(fixture.call("FSOpenFile", {kClientA, kCmdA, kPath, kMode,
                                              kHandle, kAllErrors}) == kOk,
                  "owned file handle opens");
    const auto file = fixture.handle();
    fixture.string(kPath, "/vol/content/Common");
    test::require(fixture.call("FSOpenDir", {kClientA, kCmdA, kPath, kHandle,
                                             kAllErrors}) == kOk,
                  "owned directory handle opens");
    const auto directory = fixture.handle();
    test::require(directory > file, "typed handles share monotonic IDs");

    test::require(fixture.call("FSCloseDir", {kClientA, kCmdA, file,
                                              kAllErrors}) == kFatal &&
                      fixture.call("FSGetLastError", {kClientA}) ==
                          kErrorInvalidDir,
                  "wrong handle type rejected");
    test::require(fixture.call("FSCloseFile", {kClientB, kCmdB, file,
                                               kAllErrors}) == kFatal &&
                      fixture.call("FSGetLastError", {kClientB}) ==
                          kErrorInvalidFile,
                  "wrong client cannot close handle");
    test::require(fixture.call("FSGetLastError", {kClientA}) ==
                      kErrorInvalidDir,
                  "detailed errors remain per client");
    test::require(fixture.call("FSDelClient", {kClientA, kAllErrors}) == kOk,
                  "client cleanup closes every owned handle");
    test::require(fixture.call("FSCloseFile", {kClientA, kCmdA, file,
                                               kAllErrors}) == kFatal &&
                      fixture.call("FSCloseDir", {kClientA, kCmdA, directory,
                                                  kAllErrors}) == kFatal,
                  "deleted client cannot use cleaned handles");

    test::require(fixture.call("FSAddClient", {kClientA, kAllErrors}) == kOk,
                  "client address may be registered again");
    fixture.string(kPath, "/vol/content/Common/test.bin");
    test::require(fixture.call("FSOpenFile", {kClientA, kCmdA, kPath, kMode,
                                              kHandle, kAllErrors}) == kOk &&
                      fixture.handle() > directory,
                  "closed handles are never reused");
    const auto replacement = fixture.handle();
    test::require(fixture.call("FSCloseFile", {kClientA, kCmdA, file,
                                               kAllErrors}) == kFatal &&
                      fixture.call("FSCloseFile", {kClientA, kCmdA,
                                                   replacement,
                                                   kAllErrors}) == kOk,
                  "stale handle cannot alias replacement");
}

void test_save_open_preflights_output_handle_before_host_io() {
    TempTree tree;
    Fixture fixture(tree.title, tree.save);
    fixture.initialize();
    test::require(fixture.call("SAVEInit") == kOk &&
                      fixture.call("SAVEInitSaveDir", {4}) == kOk,
                  "save fixture initialized");

    constexpr uint32_t partial_handle = 0x20000000;
    fixture.image.memory.map(partial_handle, 2, {true, true, false});
    fixture.string(kMode, "w");

    fixture.string(kPath, "created.bin");
    CPUContext create_cpu;
    const auto create_stop = fixture.execute(
        create_cpu, "SAVEOpenFile",
        {kClientA, kCmdA, 4, kPath, kMode, partial_handle, kAllErrors});
    test::require(
        create_stop.category == StopCategory::guest_fault &&
            !std::filesystem::exists(tree.save / "slot-4" / "created.bin"),
        "invalid output handle cannot create a save file");

    const auto existing = tree.save / "slot-4" / "existing.bin";
    TempTree::write(existing, "original");
    fixture.string(kPath, "existing.bin");
    CPUContext truncate_cpu;
    const auto truncate_stop = fixture.execute(
        truncate_cpu, "SAVEOpenFile",
        {kClientA, kCmdA, 4, kPath, kMode, partial_handle, kAllErrors});
    test::require(truncate_stop.category == StopCategory::guest_fault &&
                      TempTree::read(existing) == "original",
                  "invalid output handle cannot truncate a save file");
}

void test_save_write_preflights_entire_guest_buffer() {
    TempTree tree;
    Fixture fixture(tree.title, tree.save);
    fixture.initialize();
    test::require(fixture.call("SAVEInit") == kOk &&
                      fixture.call("SAVEInitSaveDir", {5}) == kOk,
                  "save fixture initialized");

    const auto file = tree.save / "slot-5" / "partial.bin";
    TempTree::write(file, "original");
    fixture.string(kPath, "partial.bin");
    fixture.string(kMode, "r+");
    test::require(fixture.call("SAVEOpenFile", {kClientA, kCmdA, 5, kPath,
                                                kMode, kHandle,
                                                kAllErrors}) == kOk,
                  "partial-write fixture opens");
    const auto handle = fixture.handle();

    constexpr uint32_t partial_buffer = 0x20001000;
    fixture.image.memory.map(partial_buffer, 4096, {true, true, false});
    CPUContext cpu;
    const auto stop = fixture.execute(
        cpu, "FSWriteFile",
        {kClientA, kCmdA, partial_buffer, 1, 4097, handle, 0, kAllErrors});
    test::require(stop.category == StopCategory::guest_fault,
                  "partially mapped write buffer faults");
    test::require(fixture.call("FSCloseFile", {kClientA, kCmdA, handle,
                                               kAllErrors}) == kOk &&
                      TempTree::read(file) == "original",
                  "guest fault cannot partially write a save file");
}

void test_save_init_dir_uses_declared_save_status() {
    TempTree tree;
    Fixture fixture(tree.title, tree.save);
    fixture.initialize();
    test::require(fixture.call("SAVEInitSaveDir", {6}) == kNotFound,
                  "uninitialized save directory reports SAVE not-found");
}

void test_modes_save_absence_state_and_exception_translation() {
    TempTree tree;
    Fixture state(tree.title, tree.save);
    test::require(state.call("FSAddClient", {kClientA, kAllErrors}) == kFatal,
                  "client add before FSInit fails");
    state.call("FSInit");
    test::require(state.call("FSAddClient", {kClientA, kAllErrors}) == kOk &&
                      state.call("FSAddClient", {kClientA, kAllErrors}) ==
                          kAlreadyOpen,
                  "client registration is stateful");
    state.string(kPath, "/vol/content/Common/test.bin");
    test::require(state.call("FSGetStat", {kClientA, kCmdA, kPath, kStat,
                                           kAllErrors}) == kFatal &&
                      state.call("FSGetLastError", {kClientA}) ==
                          kErrorInvalidParam,
                  "uninitialized command block rejected");
    state.call("FSShutdown");
    test::require(state.call("FSGetLastError", {kClientA}) == kErrorNotInit &&
                      state.call("FSGetVolumeState", {kClientA}) == 11,
                  "shutdown invalidates filesystem state");

    Fixture absent(tree.title, std::nullopt);
    absent.initialize();
    absent.string(kPath, "missing.bin");
    absent.string(kMode, "w");
    test::require(absent.call("SAVEInit") == kNotFound &&
                      absent.call("SAVEInitSaveDir", {1}) == kNotFound &&
                      absent.call("SAVEOpenFile", {kClientA, kCmdA, 1, kPath,
                                                   kMode, kHandle,
                                                   kAllErrors}) == kNotFound,
                  "save APIs fail deterministically without explicit root");

    Fixture modes(tree.title, tree.save);
    modes.initialize();
    test::require(modes.call("SAVEInit") == kOk &&
                      modes.call("SAVEInitSaveDir", {7}) == kOk,
                  "save mode fixture initialized");
    modes.string(kPath, "modes.bin");
    for (const auto mode :
         {"w", "wb", "w+", "w+b", "a", "ab", "a+", "a+b"}) {
        modes.string(kMode, mode);
        test::require(modes.call("SAVEOpenFile", {kClientA, kCmdA, 7, kPath,
                                                  kMode, kHandle,
                                                  kAllErrors}) == kOk,
                      std::string{"save mode accepted: "} + mode);
        test::require(modes.call("FSCloseFile", {kClientA, kCmdA,
                                                 modes.handle(),
                                                 kAllErrors}) == kOk,
                      "mode handle closes");
    }
    for (const auto mode : {"r", "rb", "r+", "r+b"}) {
        modes.string(kMode, mode);
        test::require(modes.call("SAVEOpenFile", {kClientA, kCmdA, 7, kPath,
                                                  kMode, kHandle,
                                                  kAllErrors}) == kOk,
                      std::string{"save read mode accepted: "} + mode);
        modes.call("FSCloseFile",
                   {kClientA, kCmdA, modes.handle(), kAllErrors});
    }
    for (const auto mode : {"x", "bw", "br", "ba+"}) {
        modes.string(kMode, mode);
        test::require(modes.call("SAVEOpenFile", {kClientA, kCmdA, 7, kPath,
                                                  kMode, kHandle,
                                                  kAllErrors}) == kFatal &&
                          modes.call("FSGetLastError", {kClientA}) ==
                              kErrorInvalidParam,
                      std::string{"unknown mode rejected: "} + mode);
    }
    modes.string(kPath, "/absolute.bin");
    modes.string(kMode, "w");
    test::require(modes.call("SAVEOpenFile", {kClientA, kCmdA, 7, kPath,
                                              kMode, kHandle,
                                              kAllErrors}) == kFatal,
                  "absolute save guest path rejected");

    modes.string(kPath, "/vol/content/Common/test.bin");
    for (const auto mode : {"r+", "w", "w+", "a", "a+"}) {
        modes.string(kMode, mode);
        test::require(modes.call("FSOpenFile", {kClientA, kCmdA, kPath, kMode,
                                                kHandle, kAllErrors}) ==
                          kPermission,
                      std::string{"content write mode denied: "} + mode);
    }

    std::filesystem::create_directories(tree.save / "slot-3");
    std::error_code symlink_error;
    std::filesystem::create_directory_symlink(
        "slot-3", tree.save / "slot-2", symlink_error);
    test::require(!symlink_error, "save slot symlink fixture created");
    Fixture aliased(tree.title, tree.save);
    aliased.initialize();
    test::require(aliased.call("SAVEInit") == kOk &&
                      aliased.call("SAVEInitSaveDir", {2}) == kStorageFull,
                  "save slot directory symlink rejected");

    const auto bad_root = tree.root / "bad-save";
    TempTree::write(bad_root, "not a directory");
    Fixture bad(tree.title, bad_root);
    bad.initialize();
    test::require(bad.call("SAVEInit") == kOk &&
                      bad.call("SAVEInitSaveDir", {3}) == kStorageFull,
                  "host filesystem failure translated at Cafe boundary");

    modes.string(kPath, "/vol/content/Common/missing.bin");
    modes.string(kMode, "r");
    test::require(modes.call("FSOpenFile", {kClientA, kCmdA, kPath, kMode,
                                            kHandle, kAllErrors}) == kNotFound &&
                      modes.call("FSGetLastError", {kClientA}) ==
                          kErrorNotFound,
                  "not-found status and detail translated");
}
} // namespace

int main() {
    test_exact_imports_and_end_to_end_io();
    test_path_validation_containment_and_deterministic_directories();
    test_typed_owned_monotonic_handles_and_client_errors();
    test_save_open_preflights_output_handle_before_host_io();
    test_save_write_preflights_entire_guest_buffer();
    test_save_init_dir_uses_declared_save_status();
    test_modes_save_absence_state_and_exception_translation();
}
