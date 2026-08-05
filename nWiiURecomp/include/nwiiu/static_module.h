#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NWIIU_STATIC_MODULE_ABI_VERSION 3u
#define NWIIU_STATIC_RESULT_MISS 0u
#define NWIIU_STATIC_RESULT_EXECUTED 1u
#define NWIIU_STATIC_RESULT_FAULT 2u

typedef struct nwiiu_static_cpu_state {
    uint32_t gpr[32];
    uint64_t fpr[32][2];
    uint8_t cr[8];
    uint32_t xer;
    uint32_t lr;
    uint32_t ctr;
    uint32_t pc;
    uint32_t fpscr;
    uint32_t reservation_address;
    uint32_t reservation_value;
    uint8_t reservation_valid;
    uint64_t instruction_count;
} nwiiu_static_cpu_state;

typedef struct nwiiu_static_memory {
    void* context;
    uint8_t (*read8)(void* context, uint32_t address);
    uint16_t (*read16)(void* context, uint32_t address);
    uint32_t (*read32)(void* context, uint32_t address);
    uint64_t (*read64)(void* context, uint32_t address);
    void (*write8)(void* context, uint32_t address, uint8_t value);
    void (*write16)(void* context, uint32_t address, uint16_t value);
    void (*write32)(void* context, uint32_t address, uint32_t value);
    void (*write64)(void* context, uint32_t address, uint64_t value);
    void (*read_bytes)(void* context, uint32_t address, uint8_t* output,
                       uint32_t size);
    void (*write_bytes)(void* context, uint32_t address,
                        const uint8_t* input, uint32_t size);
    /* Optional flat mapping of the guest address space, big-endian, valid for
       [0, flat_size). When non-null the module bypasses the callbacks above and
       compiles accesses to direct indexed loads/stores. Callbacks stay the
       fallback for hosts that cannot expose a contiguous mapping. */
    const void* flat_base;
    uint64_t flat_size;
    /* Instruction words the host rewrote after the module was generated. Blocks
       covering any of these bake stale code and must be left to the host. */
    const uint32_t* patched_addresses;
    uint32_t patched_count;
} nwiiu_static_memory;

typedef uint32_t (*nwiiu_static_run)(nwiiu_static_cpu_state* cpu,
                                     const nwiiu_static_memory* memory,
                                     uint64_t instruction_limit);

typedef struct nwiiu_static_module {
    uint32_t abi_version;
    uint32_t struct_size;
    uint64_t title_id;
    uint32_t title_version;
    uint32_t reserved;
    nwiiu_static_run run;
} nwiiu_static_module;

const nwiiu_static_module* nwiiu_static_module_v1(void);

#ifdef __cplusplus
}
#endif
