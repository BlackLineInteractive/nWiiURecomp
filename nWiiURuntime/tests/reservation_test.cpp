#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"
#include "test_support.h"

#include <cstdint>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::GuestMemory;
using nwii::runtime::MemoryPermissions;

constexpr uint32_t kAddress = 0x02001000;

GuestMemory make_memory() {
    GuestMemory memory;
    memory.map(0x02000000, 0x2000, MemoryPermissions{true, true, false});
    return memory;
}

bool stwcx_succeeded(const CPUContext& cpu) {
    // stwcx. reports success in CR0[EQ], which is bit 1 of the packed field.
    return (cpu.cr[0] & 2) != 0;
}

// The reservation must survive an uncontended read-modify-write.
void test_uncontended_sequence_succeeds() {
    auto memory = make_memory();
    CPUContext cpu;
    cpu.gpr[3] = kAddress;
    memory.write32(kAddress, 0x11111111, 0);

    nwii::runtime::ppc::lwarx(cpu, memory, 4, 0, 3);
    test::require(cpu.gpr[4] == 0x11111111, "lwarx loads the reserved word");
    cpu.gpr[5] = 0x22222222;
    nwii::runtime::ppc::stwcx(cpu, memory, 5, 0, 3);

    test::require(stwcx_succeeded(cpu), "uncontended stwcx. succeeds");
    test::require(memory.read32(kAddress, 0) == 0x22222222,
                  "uncontended stwcx. stores");
}

// The bug this guards: an address-only reservation cannot see a conflicting
// write, so the store lands on top of another thread's update.
void test_conflicting_write_breaks_reservation() {
    auto memory = make_memory();
    CPUContext cpu;
    cpu.gpr[3] = kAddress;
    memory.write32(kAddress, 0x11111111, 0);

    nwii::runtime::ppc::lwarx(cpu, memory, 4, 0, 3);
    memory.write32(kAddress, 0xDEADBEEF, 0);  // another thread gets there first

    cpu.gpr[5] = 0x22222222;
    nwii::runtime::ppc::stwcx(cpu, memory, 5, 0, 3);

    test::require(!stwcx_succeeded(cpu),
                  "stwcx. fails when the reserved word changed");
    test::require(memory.read32(kAddress, 0) == 0xDEADBEEF,
                  "a failed stwcx. must not clobber the conflicting write");
}

// A write that restores the original value leaves the reservation usable;
// PowerPC permits this, and it keeps the check a value comparison.
void test_restored_value_still_succeeds() {
    auto memory = make_memory();
    CPUContext cpu;
    cpu.gpr[3] = kAddress;
    memory.write32(kAddress, 0x11111111, 0);

    nwii::runtime::ppc::lwarx(cpu, memory, 4, 0, 3);
    memory.write32(kAddress, 0x99999999, 0);
    memory.write32(kAddress, 0x11111111, 0);

    cpu.gpr[5] = 0x22222222;
    nwii::runtime::ppc::stwcx(cpu, memory, 5, 0, 3);
    test::require(stwcx_succeeded(cpu),
                  "a restored value leaves the reservation intact");
}

void test_stwcx_without_reservation_fails() {
    auto memory = make_memory();
    CPUContext cpu;
    cpu.gpr[3] = kAddress;
    memory.write32(kAddress, 0x11111111, 0);

    cpu.gpr[5] = 0x22222222;
    nwii::runtime::ppc::stwcx(cpu, memory, 5, 0, 3);
    test::require(!stwcx_succeeded(cpu), "stwcx. without lwarx fails");
    test::require(memory.read32(kAddress, 0) == 0x11111111,
                  "stwcx. without lwarx stores nothing");
}

void test_reservation_is_consumed() {
    auto memory = make_memory();
    CPUContext cpu;
    cpu.gpr[3] = kAddress;
    memory.write32(kAddress, 0x11111111, 0);

    nwii::runtime::ppc::lwarx(cpu, memory, 4, 0, 3);
    cpu.gpr[5] = 0x22222222;
    nwii::runtime::ppc::stwcx(cpu, memory, 5, 0, 3);
    test::require(stwcx_succeeded(cpu), "first stwcx. succeeds");

    cpu.gpr[5] = 0x33333333;
    nwii::runtime::ppc::stwcx(cpu, memory, 5, 0, 3);
    test::require(!stwcx_succeeded(cpu),
                  "a second stwcx. fails, the reservation is consumed");
    test::require(memory.read32(kAddress, 0) == 0x22222222,
                  "the consumed reservation stores nothing further");
}
}

int main() {
    test_uncontended_sequence_succeeds();
    test_conflicting_write_breaks_reservation();
    test_restored_value_still_succeeds();
    test_stwcx_without_reservation_fails();
    test_reservation_is_consumed();
    return 0;
}
