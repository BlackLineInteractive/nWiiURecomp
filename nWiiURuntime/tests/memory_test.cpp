#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <exception>

namespace {
using nwii::runtime::GuestFault;
using nwii::runtime::GuestMemory;
using nwii::runtime::MemoryAccess;

template <typename Function>
void require_fault(Function&& function, uint32_t address, uint32_t width,
                   uint32_t pc, MemoryAccess access, const char* message) {
    try {
        function();
    } catch (const GuestFault& fault) {
        test::require(fault.address == address, message);
        test::require(fault.width == width, message);
        test::require(fault.pc == pc, message);
        test::require(fault.access == access, message);
        return;
    } catch (const std::exception&) {
        test::require(false, message);
    }
    test::require(false, message);
}
struct CallbackMemory {
    std::array<uint8_t, 32> bytes{};
};

uint8_t callback_read8(void* context, uint32_t address) {
    return static_cast<CallbackMemory*>(context)->bytes.at(address);
}

uint16_t callback_read16(void* context, uint32_t address) {
    return static_cast<uint16_t>(callback_read8(context, address) << 8 |
                                 callback_read8(context, address + 1));
}

uint32_t callback_read32(void* context, uint32_t address) {
    return static_cast<uint32_t>(callback_read16(context, address)) << 16 |
           callback_read16(context, address + 2);
}

uint64_t callback_read64(void* context, uint32_t address) {
    return static_cast<uint64_t>(callback_read32(context, address)) << 32 |
           callback_read32(context, address + 4);
}

void callback_write8(void* context, uint32_t address, uint8_t value) {
    static_cast<CallbackMemory*>(context)->bytes.at(address) = value;
}

void callback_write16(void* context, uint32_t address, uint16_t value) {
    callback_write8(context, address, static_cast<uint8_t>(value >> 8));
    callback_write8(context, address + 1, static_cast<uint8_t>(value));
}

void callback_write32(void* context, uint32_t address, uint32_t value) {
    callback_write16(context, address, static_cast<uint16_t>(value >> 16));
    callback_write16(context, address + 2, static_cast<uint16_t>(value));
}

void callback_write64(void* context, uint32_t address, uint64_t value) {
    callback_write32(context, address, static_cast<uint32_t>(value >> 32));
    callback_write32(context, address + 4, static_cast<uint32_t>(value));
}

void callback_read_bytes(void* context, uint32_t address, uint8_t* output,
                         uint32_t size) {
    for (uint32_t index = 0; index < size; ++index) {
        output[index] = callback_read8(context, address + index);
    }
}

void callback_write_bytes(void* context, uint32_t address,
                          const uint8_t* input, uint32_t size) {
    for (uint32_t index = 0; index < size; ++index) {
        callback_write8(context, address + index, input[index]);
    }
}
}

int main() {
    GuestMemory sparse;
    sparse.map(0x20000000, 0x20000000, {true, true, false});
    test::require(sparse.mapped_bytes() == 0x20000000ULL, "mapped bytes");
    test::require(sparse.resident_bytes() == 0,
                  "untouched reservation is sparse");
    test::require(sparse.read32(0x20001000, 0x1000) == 0,
                  "untouched pages read as zero");
    test::require(sparse.resident_bytes() == 0,
                  "zero-filled reads do not allocate");
    sparse.write32(0x20000FFE, 0x12345678, 0x1000);
    test::require(sparse.read32(0x20000FFE, 0x1004) == 0x12345678,
                  "cross-page big-endian access");
    test::require(sparse.resident_bytes() == 0x2000,
                  "two resident pages");

    GuestMemory memory;
    memory.map(0x1000, 0x20, {true, true, false});
    test::require(memory.read64(0x1010, 0x2000) == 0,
                  "mapped memory starts zero-filled");

    memory.write8(0x1000, 0x12, 0x2000);
    memory.write16(0x1002, 0x3456, 0x2000);
    memory.write32(0x1004, 0x789ABCDE, 0x2000);
    memory.write64(0x1008, 0x0123456789ABCDEFULL, 0x2000);
    test::require(memory.read8(0x1000, 0x2000) == 0x12, "read8");
    test::require(memory.read16(0x1002, 0x2000) == 0x3456,
                  "big-endian read16");
    test::require(memory.read32(0x1004, 0x2000) == 0x789ABCDE,
                  "big-endian read32");
    test::require(memory.read64(0x1008, 0x2000) == 0x0123456789ABCDEFULL,
                  "big-endian read64");

    memory.map(0x1020, 0x20, {true, true, false});
    std::array<uint8_t, 12> bulk{};
    memory.write_bytes(0x1018, std::array<uint8_t, 12>{
                                   0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
                       0x2000);
    memory.read_bytes(0x1018, bulk, 0x2000);
    test::require(
        bulk == std::array<uint8_t, 12>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
        "bulk access crosses adjacent mappings");
    memory.fill(0x101C, 8, std::array<uint8_t, 4>{0x11, 0x22, 0x33, 0x44},
                0x2000);
    memory.read_bytes(0x1018, bulk, 0x2000);
    test::require(
        bulk == std::array<uint8_t, 12>{0, 1, 2, 3, 0x11, 0x22, 0x33, 0x44,
                                        0x11, 0x22, 0x33, 0x44},
        "pattern fill preserves byte order");

    const std::array<uint8_t, 4> initial{0xDE, 0xAD, 0xBE, 0xEF};
    memory.map(0x2000, 4, {true, false, false}, initial);
    test::require(memory.read32(0x2000, 0) == 0xDEADBEEF,
                  "initial bytes are copied");
    require_fault([&] { memory.write8(0x2000, 0, 0x2222); }, 0x2000, 1,
                  0x2222, MemoryAccess::write,
                  "initial bytes do not change permissions");

    test::require_throws(
        [&] { memory.map(0x1008, 8, {true, false, false}); }, "overlap",
        "overlapping map rejected");
    test::require_throws(
        [&] { memory.map(0xFFFFFFF0, 0x20, {true, false, false}); }, "wrap",
        "wrapping map rejected");
    test::require_throws(
        [&] { memory.map(0x4000, 0, {true, false, false}); }, "zero",
        "zero-sized map rejected");

    require_fault([&] { (void)memory.read16(0x101F, 0x3333); }, 0x101F, 2,
                  0x3333, MemoryAccess::read, "cross-region read faults");

    memory.validate_range(0x1018, 0x10, 0x3334, MemoryAccess::read);
    require_fault(
        [&] { memory.validate(0x1018, 0x10, 0x3334, MemoryAccess::read); },
        0x1018, 0x10, 0x3334, MemoryAccess::read,
        "scalar validation retains single-mapping contract");
    memory.validate_range(0xDEADBEEF, 0, 0x3335, MemoryAccess::write);
    require_fault(
        [&] {
            memory.validate_range(0x1038, 0x10, 0x3336,
                                  MemoryAccess::read);
        },
        0x1038, 0x10, 0x3336, MemoryAccess::read,
        "range validation rejects mapping gaps");

    memory.map(0x4000, 8, {true, true, false});
    memory.map(0x4008, 8, {false, true, false});
    require_fault(
        [&] {
            memory.validate_range(0x4004, 8, 0x3337, MemoryAccess::read);
        },
        0x4004, 8, 0x3337, MemoryAccess::read,
        "range validation checks every mapping permission");

    memory.map(0xFFFFFFF0, 0x10, {true, true, false});
    memory.validate_range(0xFFFFFFF8, 8, 0x3338, MemoryAccess::read);
    require_fault(
        [&] {
            memory.validate_range(0xFFFFFFF8, 9, 0x3339,
                                  MemoryAccess::read);
        },
        0xFFFFFFF8, 9, 0x3339, MemoryAccess::read,
        "range validation rejects address-space wrap");
    require_fault([&] { (void)memory.read32(0x9000, 0x4444); }, 0x9000, 4,
                  0x4444, MemoryAccess::read, "unmapped read faults");

    const std::array<uint8_t, 8> code{0x60, 0x00, 0x00, 0x00,
                                      0x4E, 0x80, 0x00, 0x20};
    memory.map(0x3000, code.size(), {true, false, true}, code);
    test::require(memory.fetch32(0x3000) == 0x60000000,
                  "fetch32 reads executable big-endian instruction");
    require_fault([&] { (void)memory.fetch32(0x2000); }, 0x2000, 4, 0x2000,
                  MemoryAccess::execute, "fetch32 requires execute permission");
    require_fault([&] { (void)memory.fetch32(0x3002); }, 0x3002, 4, 0x3002,
                  MemoryAccess::execute, "fetch32 requires alignment");

    memory.patch16(0x2000, 0x1234);
    test::require(memory.read16(0x2000, 0) == 0x1234,
                  "patch16 bypasses guest write permission");
    memory.patch32(0x2000, 0x89ABCDEF);
    test::require(memory.read32(0x2000, 0) == 0x89ABCDEF,
                  "patch32 bypasses guest write permission");
    require_fault([&] { memory.patch16(0x2003, 0); }, 0x2003, 2, 0,
                  MemoryAccess::write, "patch16 enforces bounds");
    require_fault([&] { memory.patch32(0x2001, 0); }, 0x2001, 4, 0,
                  MemoryAccess::write, "patch32 enforces bounds");

    CallbackMemory callback_storage;
    GuestMemory callback_memory(nwii::runtime::GuestMemoryCallbacks{
        &callback_storage,
        callback_read8,
        callback_read16,
        callback_read32,
        callback_read64,
        callback_write8,
        callback_write16,
        callback_write32,
        callback_write64,
        callback_read_bytes,
        callback_write_bytes,
    });
    callback_memory.write64(4, 0x0123456789ABCDEFULL, 0x2000);
    test::require(callback_memory.read64(4, 0x2004) ==
                      0x0123456789ABCDEFULL,
                  "callback memory delegates scalar accesses");
    callback_memory.fill(12, 4, std::array<uint8_t, 2>{0xAA, 0x55}, 0x2008);
    std::array<uint8_t, 4> callback_bytes{};
    callback_memory.read_bytes(12, callback_bytes, 0x200C);
    test::require(callback_bytes ==
                      std::array<uint8_t, 4>{0xAA, 0x55, 0xAA, 0x55},
                  "callback memory delegates byte accesses and fill");
    callback_memory.validate_range(0, 0x20, 0x2010, MemoryAccess::execute);

    // Flat mapping. The buffer is far smaller than the span it claims; only
    // the low bytes are ever touched, and flat_size only gates adoption.
    std::array<uint8_t, 64> flat_storage{};
    auto flat_callbacks = nwii::runtime::GuestMemoryCallbacks{
        &callback_storage,   callback_read8,     callback_read16,
        callback_read32,     callback_read64,    callback_write8,
        callback_write16,    callback_write32,   callback_write64,
        callback_read_bytes, callback_write_bytes,
    };
    flat_callbacks.flat_base = flat_storage.data();
    flat_callbacks.flat_size = uint64_t{1} << 32;
    GuestMemory flat_memory(flat_callbacks);

    flat_memory.write32(0, 0x01234567u, 0);
    test::require(flat_storage[0] == 0x01 && flat_storage[1] == 0x23 &&
                      flat_storage[2] == 0x45 && flat_storage[3] == 0x67,
                  "flat write32 stores guest big-endian order");
    test::require(flat_memory.read32(0, 0) == 0x01234567u,
                  "flat read32 round-trips");
    flat_memory.write16(8, 0xBEEF, 0);
    test::require(flat_storage[8] == 0xBE && flat_storage[9] == 0xEF,
                  "flat write16 stores guest big-endian order");
    test::require(flat_memory.read16(8, 0) == 0xBEEF,
                  "flat read16 round-trips");
    flat_memory.write64(16, 0x0123456789ABCDEFULL, 0);
    test::require(flat_storage[16] == 0x01 && flat_storage[23] == 0xEF,
                  "flat write64 stores guest big-endian order");
    test::require(flat_memory.read64(16, 0) == 0x0123456789ABCDEFULL,
                  "flat read64 round-trips");
    flat_memory.write8(30, 0x5A, 0);
    test::require(flat_memory.read8(30, 0) == 0x5A, "flat read8 round-trips");

    const std::array<uint8_t, 3> flat_input{0xDE, 0xAD, 0xC0};
    flat_memory.write_bytes(40, flat_input, 0);
    std::array<uint8_t, 3> flat_output{};
    flat_memory.read_bytes(40, flat_output, 0);
    test::require(flat_output == flat_input,
                  "flat byte copies round-trip");
    test::require(flat_storage[40] == 0xDE,
                  "flat write_bytes writes through to the mapping");

    // A mapping that cannot cover every uint32_t address must be refused:
    // the fast path indexes unchecked, so a short one would read out of bounds.
    CallbackMemory partial_storage;
    auto partial_callbacks = flat_callbacks;
    partial_callbacks.context = &partial_storage;
    partial_callbacks.flat_size = 0x1000;
    GuestMemory partial_memory(partial_callbacks);
    partial_memory.write32(4, 0xCAFEBABEu, 0);
    test::require(partial_storage.bytes[4] == 0xCA && flat_storage[4] == 0x00,
                  "short flat mapping is refused and falls back to callbacks");

    const nwii::runtime::CPUContext context;
    test::require(context.gpr == std::array<uint32_t, 32>{}, "zero GPRs");
    test::require(
        context.fpr ==
            std::array<std::array<uint64_t, 2>, 32>{},
        "zero FPR lanes");
    test::require(context.cr == std::array<uint8_t, 8>{}, "zero CR fields");
    test::require(context.xer == 0 && context.lr == 0 && context.ctr == 0 &&
                      context.pc == 0 && context.fpscr == 0,
                  "zero scalar registers");
    test::require(context.reservation_address == 0xFFFFFFFF &&
                      !context.reservation_valid,
                  "default reservation state");
    test::require(context.instruction_count == 0 &&
                      context.pc_history == std::array<uint32_t, 32>{} &&
                      context.history_size == 0 && context.history_cursor == 0,
                  "zero execution history");
    test::require(context.running, "CPU starts running");
}
