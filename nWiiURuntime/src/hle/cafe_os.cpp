#include "runtime/cafe_os.h"
#include "runtime/config.h"
#include "common/endian.h"
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __has_include
#  if __has_include(<zlib.h>)
#    include <zlib.h>
#    define HAVE_ZLIB 1
#  endif
#endif

namespace nwii::runtime::cafe {

static std::unordered_map<std::string, RPLModule> g_rpl_modules;

void register_rpl_module(const RPLModule& module) {
    std::cout << "[Cafe OS] RPL: " << module.name
              << " base=0x" << std::hex << module.base_address << std::dec
              << " imports=" << module.imports.size()
              << " exports=" << module.exports.size() << std::endl;
    g_rpl_modules[module.name] = module;
}

RPLModule* find_rpl_module(const std::string& name) {
    auto it = g_rpl_modules.find(name);
    return (it != g_rpl_modules.end()) ? &it->second : nullptr;
}

static bool decompress_section(const uint8_t* src, uint32_t src_size,
                                std::vector<uint8_t>& out) {
#ifdef HAVE_ZLIB
    uint32_t decompressed_size = 0;
    std::memcpy(&decompressed_size, src, 4); // first 4 bytes = uncompressed size (BE)
    decompressed_size = nwii::swap_endian(decompressed_size);
    out.resize(decompressed_size);
    uLongf dest_len = decompressed_size;
    if (uncompress(out.data(), &dest_len, src + 4, src_size - 4) != Z_OK)
        return false;
    out.resize(dest_len);
    return true;
#else
    (void)src; (void)src_size; (void)out;
    std::cerr << "[Cafe OS] zlib not available; cannot decompress RPL section" << std::endl;
    return false;
#endif
}

bool load_rpx(CPUContext& ctx, const std::string& path) {
    std::cout << "[Cafe OS] Loading RPX: " << path << std::endl;

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) {
        std::cerr << "[Cafe OS] Cannot open: " << path << std::endl;
        return false;
    }

    // Read ELF32 header
    RPLHeader hdr;
    f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr));
    if (!f) {
        std::cerr << "[Cafe OS] Short read on RPX header" << std::endl;
        return false;
    }

    // Verify ELF magic
    if (hdr.e_ident[0] != 0x7F || hdr.e_ident[1] != 'E' ||
        hdr.e_ident[2] != 'L'  || hdr.e_ident[3] != 'F') {
        std::cerr << "[Cafe OS] Not an ELF file: " << path << std::endl;
        return false;
    }

    // Wii U RPL/RPX type = 0xFE01
    uint16_t e_type = (hdr.e_type >> 8) | ((hdr.e_type & 0xFF) << 8);
    if (e_type != 0xFE01) {
        std::cerr << "[Cafe OS] Not an RPL (e_type=0x" << std::hex << e_type
                  << std::dec << "): " << path << std::endl;
        return false;
    }

    uint16_t shnum    = nwii::swap_endian(hdr.e_shnum);
    uint16_t shentsize= nwii::swap_endian(hdr.e_shentsize);
    uint32_t shoff    = nwii::swap_endian(hdr.e_shoff);

    if (shnum == 0 || shoff == 0) {
        std::cerr << "[Cafe OS] RPX has no sections" << std::endl;
        return false;
    }

    // Read section headers
    struct Elf32Shdr {
        uint32_t sh_name, sh_type, sh_flags, sh_addr;
        uint32_t sh_offset, sh_size, sh_link, sh_info;
        uint32_t sh_addralign, sh_entsize;
    };

    f.seekg(shoff);
    std::vector<Elf32Shdr> shdrs(shnum);
    for (auto& s : shdrs) {
        f.read(reinterpret_cast<char*>(&s), sizeof(s));
        s.sh_name   = nwii::swap_endian(s.sh_name);
        s.sh_type   = nwii::swap_endian(s.sh_type);
        s.sh_flags  = nwii::swap_endian(s.sh_flags);
        s.sh_addr   = nwii::swap_endian(s.sh_addr);
        s.sh_offset = nwii::swap_endian(s.sh_offset);
        s.sh_size   = nwii::swap_endian(s.sh_size);
    }

    // Load sections into MMU
    int loaded = 0;
    for (const auto& s : shdrs) {
        if (s.sh_addr == 0 || s.sh_size == 0 || s.sh_type == 8 /*SHT_NOBITS*/)
            continue;

        std::vector<uint8_t> data(s.sh_size);
        f.seekg(s.sh_offset);
        f.read(reinterpret_cast<char*>(data.data()), s.sh_size);
        if (!f) continue;

        if (s.sh_flags & SHF_RPL_ZLIB) {
            std::vector<uint8_t> decompressed;
            if (!decompress_section(data.data(), s.sh_size, decompressed))
                continue;
            data = std::move(decompressed);
        }

        uint32_t vaddr = s.sh_addr;
        for (uint32_t i = 0; i < (uint32_t)data.size(); i++)
            ctx.mmu.write8(vaddr + i, data[i]);
        loaded++;
    }

    std::cout << "[Cafe OS] Loaded " << loaded << " sections from " << path << std::endl;
    return loaded > 0;
}

// Bump allocator for Cafe OS system memory (0x01000000-0x50000000 VA)
static uint32_t cafe_heap_ptr = 0x10000000;

uint32_t OSAllocFromSystem(CPUContext& ctx, uint32_t size, int align) {
    if (align > 0)
        cafe_heap_ptr = (cafe_heap_ptr + align - 1) & ~(align - 1);
    uint32_t ptr = cafe_heap_ptr;
    cafe_heap_ptr += size;
    std::cout << "[Cafe OS] OSAllocFromSystem size=" << size << " -> 0x"
              << std::hex << ptr << std::dec << std::endl;
    return ptr;
}

void OSFreeToSystem(CPUContext& ctx, uint32_t ptr) {
    (void)ptr; // bump allocator; free is a no-op
}

} // namespace nwii::runtime::cafe
