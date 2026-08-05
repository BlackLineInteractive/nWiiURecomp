#include "runtime/cpu_context.h"
#include "runtime/memory.h"
#include "runtime/ppc_semantics.h"
#include "test_support.h"

#include <bit>
#include <cstdint>

namespace {
using nwii::runtime::CPUContext;
using nwii::runtime::GuestMemory;

double lane(const CPUContext& cpu, uint32_t reg, int which) {
    return std::bit_cast<double>(cpu.fpr[reg][which]);
}

void set(CPUContext& cpu, uint32_t reg, double ps0, double ps1) {
    cpu.fpr[reg][0] = std::bit_cast<uint64_t>(ps0);
    cpu.fpr[reg][1] = std::bit_cast<uint64_t>(ps1);
}

// Espresso writes single-precision scalar results to both paired-single lanes.
// A stale ps1 is invisible until a later paired-single op reads it, so these
// assert the lane explicitly rather than just the arithmetic.
void test_single_precision_writes_both_lanes() {
    CPUContext cpu;
    // poison ps1 of the destination so a missing replication is detectable
    set(cpu, 5, 0.0, -3.0);
    set(cpu, 1, 2.0, 111.0);
    set(cpu, 2, 3.0, 222.0);

    nwii::runtime::ppc::fadds(cpu, 5, 1, 2);
    test::require(lane(cpu, 5, 0) == 5.0, "fadds computes ps0");
    test::require(lane(cpu, 5, 1) == 5.0,
                  "fadds replicates into ps1, it does not leave it stale");

    set(cpu, 5, 0.0, -3.0);
    nwii::runtime::ppc::fsubs(cpu, 5, 2, 1);
    test::require(lane(cpu, 5, 0) == 1.0 && lane(cpu, 5, 1) == 1.0,
                  "fsubs writes both lanes");

    set(cpu, 5, 0.0, -3.0);
    nwii::runtime::ppc::fmuls(cpu, 5, 1, 2);
    test::require(lane(cpu, 5, 0) == 6.0 && lane(cpu, 5, 1) == 6.0,
                  "fmuls writes both lanes");

    set(cpu, 5, 0.0, -3.0);
    nwii::runtime::ppc::fdivs(cpu, 5, 2, 1);
    test::require(lane(cpu, 5, 0) == 1.5 && lane(cpu, 5, 1) == 1.5,
                  "fdivs writes both lanes");

    set(cpu, 5, 0.0, -3.0);
    set(cpu, 3, 4.0, 333.0);
    nwii::runtime::ppc::fmadds(cpu, 5, 1, 3, 2);  // 2*4 + 3
    test::require(lane(cpu, 5, 0) == lane(cpu, 5, 1),
                  "fmadds writes both lanes");

    set(cpu, 5, 0.0, -3.0);
    nwii::runtime::ppc::frsp(cpu, 5, 2);
    test::require(lane(cpu, 5, 0) == 3.0 && lane(cpu, 5, 1) == 3.0,
                  "frsp writes both lanes");
}

// The double-precision forms must NOT replicate; ps1 is left alone.
// lfs and friends load a single and replicate it into both lanes (PPC_LSQE).
void test_single_load_writes_both_lanes() {
    GuestMemory memory;
    memory.map(0x02000000, 0x1000,
               nwii::runtime::MemoryPermissions{true, true, false});
    CPUContext cpu;
    cpu.gpr[3] = 0x02000000;
    // 2.5f big-endian
    memory.write32(0x02000000, 0x40200000, 0);

    set(cpu, 7, 0.0, -9.0);
    nwii::runtime::ppc::lfs(cpu, memory, 7, 3, 0);
    test::require(lane(cpu, 7, 0) == 2.5, "lfs loads ps0");
    test::require(lane(cpu, 7, 1) == 2.5,
                  "lfs replicates into ps1, it does not leave it stale");

    set(cpu, 7, 0.0, -9.0);
    cpu.gpr[4] = 0;
    nwii::runtime::ppc::lfsx(cpu, memory, 7, 3, 4);
    test::require(lane(cpu, 7, 0) == 2.5 && lane(cpu, 7, 1) == 2.5,
                  "lfsx writes both lanes");
}

void test_double_precision_leaves_ps1() {
    CPUContext cpu;
    set(cpu, 5, 0.0, -3.0);
    set(cpu, 1, 2.0, 111.0);
    set(cpu, 2, 3.0, 222.0);

    nwii::runtime::ppc::fadd(cpu, 5, 1, 2);
    test::require(lane(cpu, 5, 0) == 5.0, "fadd computes ps0");
    test::require(lane(cpu, 5, 1) == -3.0,
                  "fadd is double precision and must not touch ps1");
}
}

int main() {
    test_single_precision_writes_both_lanes();
    test_single_load_writes_both_lanes();
    test_double_precision_leaves_ps1();
    return 0;
}
