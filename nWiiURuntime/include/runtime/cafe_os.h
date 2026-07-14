#pragma once
#include <cstdint>

namespace nwii::runtime {
    struct CPUContext;
}
#include <string>
#include <unordered_map>
#include <vector>

namespace nwii::runtime::cafe {

// RPL/RPX Binary Format (Wii U executables)
// Based on: decaf-emu src/loader, rpl2elf, WiiUBrew documentation

// RPL Section Header Flags
constexpr uint32_t SHF_RPL_ZLIB = 0x08000000; // Section is zlib compressed
constexpr uint32_t SHF_RPL_FILEINFO = 0x10000000;

// RPL File Header (extends standard ELF32)
struct RPLHeader {
  uint8_t e_ident[16]; // ELF magic + RPL markers
  uint16_t e_type;     // ET_CAFE_RPL = 0xFE01
  uint16_t e_machine;  // EM_PPC = 20
  uint32_t e_version;
  uint32_t e_entry;
  uint32_t e_phoff;
  uint32_t e_shoff;
  uint32_t e_flags;
  uint16_t e_ehsize;
  uint16_t e_phentsize;
  uint16_t e_phnum;
  uint16_t e_shentsize;
  uint16_t e_shnum;
  uint16_t e_shstrndx;
};

// RPL Import/Export Entry
struct RPLImport {
  std::string name;     // Function name (e.g. "OSReport")
  std::string rpl_name; // Source RPL (e.g. "coreinit.rpl")
  uint32_t address;     // Virtual address in the loaded binary
};

// RPL Module (loaded dynamic library)
struct RPLModule {
  std::string name;
  uint32_t base_address;
  std::vector<RPLImport> imports;
  std::vector<RPLImport> exports;
};

// Cafe OS System Calls
// Based on: Cemu src/Cafe/OS, WiiUBrew Cafe OS docs

// Memory allocation flags
constexpr uint32_t MEM_ALLOC_DEFAULT = 0;
constexpr uint32_t MEM_ALLOC_ZERO = 1;

// Thread priority range
constexpr int32_t OS_THREAD_PRIORITY_MIN = 0;
constexpr int32_t OS_THREAD_PRIORITY_MAX = 31;
constexpr int32_t OS_THREAD_PRIORITY_DEFAULT = 16;

// Latte GPU (AMD R7xx-based) - PM4 Command Opcodes
// Based on: AMD R6xx/R7xx programming guides, Cemu src/Cafe/HW

// PM4 Packet Types
constexpr uint32_t PM4_TYPE0 = 0; // Register write
constexpr uint32_t PM4_TYPE2 = 2; // NOP
constexpr uint32_t PM4_TYPE3 = 3; // Command with opcode

// Common PM4 Type 3 Opcodes
constexpr uint32_t PM4_NOP = 0x10;
constexpr uint32_t PM4_INDIRECT_BUFFER = 0x3F;
constexpr uint32_t PM4_MEM_WRITE = 0x3D;
constexpr uint32_t PM4_EVENT_WRITE = 0x46;
constexpr uint32_t PM4_EVENT_WRITE_EOP = 0x47;
constexpr uint32_t PM4_DRAW_INDEX_AUTO = 0x2D;
constexpr uint32_t PM4_DRAW_INDEX_2 = 0x27;
constexpr uint32_t PM4_INDEX_TYPE = 0x0A;
constexpr uint32_t PM4_SET_CONTEXT_REG = 0x69;
constexpr uint32_t PM4_SET_ALU_CONST = 0x6F;
constexpr uint32_t PM4_SET_RESOURCE = 0x6D;
constexpr uint32_t PM4_SET_SAMPLER = 0x6E;
constexpr uint32_t PM4_SURFACE_SYNC = 0x43;
constexpr uint32_t PM4_WAIT_REG_MEM = 0x3C;

// GX2 Shader Types
enum class GX2ShaderType : uint32_t {
  Vertex = 0,
  Pixel = 1,
  Geometry = 2,
};

// GX2 Surface Format (subset)
enum class GX2SurfaceFormat : uint32_t {
  UNORM_R8_G8_B8_A8 = 0x01A,
  UNORM_R5_G6_B5 = 0x008,
  UNORM_BC1 = 0x031,
  UNORM_BC3 = 0x033,
  FLOAT_R32 = 0x80E,
  FLOAT_R32_G32_B32_A32 = 0x823,
};

// Function declarations

// RPL loader
bool load_rpx(CPUContext &ctx, const std::string &path);
void register_rpl_module(const RPLModule &module);
RPLModule *find_rpl_module(const std::string &name);

// Cafe OS stubs
uint32_t OSAllocFromSystem(CPUContext &ctx, uint32_t size, int align);
void OSFreeToSystem(CPUContext &ctx, uint32_t ptr);

// Latte GPU
void process_gx2_pm4_packet(CPUContext &ctx,
                            const std::vector<uint32_t> &packet);
void GX2Init(CPUContext &ctx);
void GX2Shutdown(CPUContext &ctx);
void GX2DrawDone(CPUContext &ctx);

} // namespace nwii::runtime::cafe
