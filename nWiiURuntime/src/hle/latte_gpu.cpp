#include "runtime/cafe_os.h"
#include <cstring>
#include <iostream>
#include <vector>

namespace nwii::runtime::cafe {

// Latte GPU Emulation (AMD R7xx-based)
// Translates GX2/PM4 command streams to host OpenGL/Vulkan calls.
//
// Architecture overview:
//   Wii U games submit PM4 command buffers to the Latte GPU via GX2 API.
//   Each PM4 packet sets registers, uploads shaders, or issues draw calls.
//   Our HLE intercepts GX2 calls directly and translates to host GPU.
//
// Key difference from Wii (GX/TEV):
//   - Wii uses a fixed-function TEV pipeline with FIFO commands
//   - Wii U uses programmable shaders (vertex/pixel/geometry) with PM4 packets
//   - Shaders arrive pre-compiled in GPU machine code (not GLSL)
//   - CafeGLSL or a custom decompiler is needed to translate them

// Latte register file (subset of R7xx registers)
struct LatteRegisters {
  // Context registers (0xA000-0xAFFF range)
  uint32_t CB_COLOR0_BASE = 0; // Color buffer 0 base address
  uint32_t CB_COLOR0_SIZE = 0; // Color buffer 0 size
  uint32_t CB_COLOR0_INFO = 0; // Color buffer 0 format info
  uint32_t DB_DEPTH_BASE = 0;  // Depth buffer base address
  uint32_t DB_DEPTH_SIZE = 0;  // Depth buffer size
  uint32_t DB_DEPTH_INFO = 0;  // Depth buffer format
  uint32_t PA_CL_VTE_CNTL = 0; // Viewport transform control
  uint32_t PA_SC_SCREEN_SCISSOR_TL = 0;
  uint32_t PA_SC_SCREEN_SCISSOR_BR = 0;
  uint32_t SQ_PGM_START_VS = 0;    // Vertex shader program address
  uint32_t SQ_PGM_START_PS = 0;    // Pixel shader program address
  uint32_t SQ_PGM_START_GS = 0;    // Geometry shader program address
  uint32_t VGT_PRIMITIVE_TYPE = 0; // Current primitive type

  // ALU constants (uniform registers)
  float alu_consts[256 * 4] = {}; // 256 vec4 constants
};

static LatteRegisters g_latte_regs;

// PM4 Packet Parser

static uint32_t extract_pm4_type(uint32_t header) {
  return (header >> 30) & 0x3;
}

static uint32_t extract_pm4_opcode(uint32_t header) {
  return (header >> 8) & 0xFF;
}

static uint32_t extract_pm4_count(uint32_t header) {
  return ((header >> 16) & 0x3FFF) + 1;
}

void process_pm4_type0(const uint32_t* data, uint32_t count, uint32_t base_reg) {
    for (uint32_t i = 0; i < count; i++) {
        uint32_t reg = base_reg + i;
        uint32_t val = data[i];
        switch (reg) {
        case 0xA010: g_latte_regs.CB_COLOR0_BASE = val; break;
        case 0xA011: g_latte_regs.CB_COLOR0_SIZE = val; break;
        case 0xA01C: g_latte_regs.CB_COLOR0_INFO = val; break;
        case 0xA105: g_latte_regs.DB_DEPTH_BASE   = val; break;
        case 0xA106: g_latte_regs.DB_DEPTH_SIZE   = val; break;
        case 0xA0D4: g_latte_regs.PA_CL_VTE_CNTL  = val; break;
        case 0xA094: g_latte_regs.SQ_PGM_START_VS = val; break;
        case 0xA0A8: g_latte_regs.SQ_PGM_START_PS = val; break;
        case 0xA0B0: g_latte_regs.SQ_PGM_START_GS = val; break;
        case 0xA29B: g_latte_regs.VGT_PRIMITIVE_TYPE = val; break;
        default: break;
        }
    }
}

void process_pm4_type3(const uint32_t *data, uint32_t opcode, uint32_t count) {
  switch (opcode) {
  case PM4_NOP:
    // Do nothing
    break;

  case PM4_SET_CONTEXT_REG: {
    // Sets context registers (0xA000 base)
    if (count < 2)
      break;
    uint32_t reg_offset = data[0];
    for (uint32_t i = 1; i < count; i++) {
      uint32_t reg_addr = 0xA000 + reg_offset + (i - 1);
      uint32_t value = data[i];
      // Map known registers
      switch (reg_addr) {
      case 0xA010:
        g_latte_regs.CB_COLOR0_BASE = value;
        break;
      case 0xA011:
        g_latte_regs.CB_COLOR0_SIZE = value;
        break;
      case 0xA01C:
        g_latte_regs.CB_COLOR0_INFO = value;
        break;
      default:
        break;
      }
    }
    break;
  }

  case PM4_SET_ALU_CONST: {
    // Upload ALU constants (uniforms)
    if (count < 2)
      break;
    uint32_t const_offset = data[0];
    for (uint32_t i = 1; i < count && (const_offset + i - 1) < 1024; i++) {
      uint32_t val = data[i];
      float fval;
      std::memcpy(&fval, &val, 4);
      g_latte_regs.alu_consts[const_offset + i - 1] = fval;
    }
    break;
  }

  case PM4_DRAW_INDEX_AUTO: {
    if (count < 1) break;
    uint32_t vertex_count = data[0];
#if defined(HAVE_RLGL)
    rlBegin(GL_TRIANGLES);
    for (uint32_t v = 0; v < vertex_count; v++) {
        rlColor4ub(255, 255, 255, 255);
        rlVertex3f(0.f, 0.f, 0.f); // vertex data fed by GX2 attribute buffers
    }
    rlEnd();
#else
    (void)vertex_count;
#endif
    break;
  }

  case PM4_DRAW_INDEX_2: {
    if (count < 3) break;
    uint32_t index_count = data[2];
    // index buffer address lives in GPU virtual memory (Latte MEM2);
    // read from g_mmu when the MMU interface is plumbed in (Крок 3).
    (void)index_count;
    break;
  }

  case PM4_SURFACE_SYNC: {
    // Surface synchronization barrier
    // Ensures all previous draws/writes complete before continuing
    break;
  }

  case PM4_EVENT_WRITE_EOP: {
    // Writes a value to memory when GPU pipeline drains.
    if (count >= 4) {
        // data[1] = address low, data[2] = (address high | data_sel), data[3] = value
        // When MMU is accessible here this becomes a real fence write.
        (void)data;
    }
    break;
  }

  default:
    // Unknown PM4 opcode — log for future implementation
    std::cout << "[Latte GPU] Unhandled PM4 opcode=0x" << std::hex << opcode
              << " count=" << std::dec << count << std::endl;
    break;
  }
}

// Main entry point for processing a GX2 command buffer
void process_gx2_pm4_packet(CPUContext &ctx,
                            const std::vector<uint32_t> &packet) {
  if (packet.empty())
    return;

  size_t offset = 0;
  while (offset < packet.size()) {
    uint32_t header = packet[offset];
    uint32_t type = extract_pm4_type(header);

    if (type == PM4_TYPE0) {
      uint32_t count = ((header >> 16) & 0x3FFF) + 1;
      uint32_t base_reg = header & 0xFFFF;
      if (offset + 1 + count > packet.size())
        break;
      process_pm4_type0(&packet[offset + 1], count, base_reg);
      offset += 1 + count;
    } else if (type == PM4_TYPE2) {
      // NOP filler
      offset++;
    } else if (type == PM4_TYPE3) {
      uint32_t opcode = extract_pm4_opcode(header);
      uint32_t count = extract_pm4_count(header);
      if (offset + 1 + count > packet.size())
        break;
      process_pm4_type3(&packet[offset + 1], opcode, count);
      offset += 1 + count;
    } else {
      // Unknown packet type
      offset++;
    }
  }
}

// GX2 High-Level API Stubs
// These map directly to Cafe OS GX2 library calls

void GX2Init(CPUContext &ctx) {
  std::cout << "[Latte GPU] GX2Init: Initializing Latte GPU emulation"
            << std::endl;
  std::memset(&g_latte_regs, 0, sizeof(g_latte_regs));
}

void GX2Shutdown(CPUContext &ctx) {
  std::cout << "[Latte GPU] GX2Shutdown" << std::endl;
}

void GX2DrawDone(CPUContext &ctx) {
  // Synchronization: wait for all pending draws to complete
  // In our HLE implementation this is instant
}

} // namespace nwii::runtime::cafe
