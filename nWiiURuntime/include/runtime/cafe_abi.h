#pragma once

#include "runtime/memory.h"

#include <cstdint>

namespace nwii::runtime::abi {
constexpr uint32_t kCafeSystemStart = 0x01000000;
constexpr uint32_t kCafeSystemEnd = 0x01020000;
constexpr uint32_t kCafeSystemSize = kCafeSystemEnd - kCafeSystemStart;

// WWHD EU v0's RPX loader data, core stacks, exception stacks, and system
// heap leave page-aligned MEM2 at 0x10520000..0x4DFA0000.
constexpr uint32_t kMem1Heap = 0xF4000000;
constexpr uint32_t kMem2Heap = 0x10520000;
constexpr uint32_t kFgHeap = 0xE0000000;
constexpr uint32_t kMainThread = 0x01000000;
constexpr uint32_t kSecondThread = 0x010006A0;
constexpr uint32_t kGhsMutex = 0x01000D40;
constexpr uint32_t kOsSystemInfo = 0x01000DA0;
constexpr uint32_t kOsSystemInfoSize = 0x20;
constexpr uint32_t kBusClockSpeed = 248625000;
constexpr uint32_t kCoreClockSpeed = 1243125000;
constexpr uint32_t kGhsIob = 0x01010000;
constexpr uint32_t kGhsIobEntrySize = 16;
constexpr uint32_t kGhsIobCount = 100;
constexpr uint32_t kGhsFlocks =
    kGhsIob + kGhsIobEntrySize * kGhsIobCount;
constexpr uint32_t kGhsErrno = kGhsFlocks + kGhsIobCount * sizeof(uint32_t);

constexpr uint32_t kMem1Start = 0xF400004C;
constexpr uint32_t kMem1End = 0xF6000000;
constexpr uint32_t kMem2Start = 0x10520054;
constexpr uint32_t kMem2End = 0x4DFA0000;
constexpr uint32_t kFgStart = 0xE000004C;
constexpr uint32_t kFgEnd = 0xE2800000;
constexpr uint32_t kCafeMemorySize =
    kCafeSystemSize + (kMem1End - kMem1Heap) +
    (kMem2End - kMem2Heap) + (kFgEnd - kFgHeap);

constexpr uint32_t kOsContextSize = 0x320;
constexpr uint32_t kOsContextTagOffset = 0x00;
constexpr uint64_t kOsContextTag = 0x4F53436F6E747874ULL;
constexpr uint32_t kOsContextGprOffset = 0x08;
constexpr uint32_t kOsContextLrOffset = 0x8C;
constexpr uint32_t kOsContextSrr0Offset = 0x98;
constexpr uint32_t kOsContextFpscrOffset = 0xB4;
constexpr uint32_t kOsContextGqrOffset = 0x1BC;
constexpr uint32_t kOsContextPirOffset = 0x1DC;
constexpr uint32_t kOsContextAttrOffset = 0x304;
constexpr uint32_t kOsThreadSize = 0x6A0;
constexpr uint32_t kOsThreadTagOffset = 0x320;
constexpr uint32_t kOsThreadStateOffset = 0x324;
constexpr uint32_t kOsThreadAttrOffset = 0x325;
constexpr uint32_t kOsThreadIdOffset = 0x326;
constexpr uint32_t kOsThreadSuspendOffset = 0x328;
constexpr uint32_t kOsThreadPriorityOffset = 0x32C;
constexpr uint32_t kOsThreadBasePriorityOffset = 0x330;
constexpr uint32_t kOsThreadQueueOffset = 0x35C;
constexpr uint32_t kOsThreadExitValueOffset = 0x334;
constexpr uint32_t kOsThreadMutexOffset = 0x378;
constexpr uint32_t kOsThreadStackStartOffset = 0x394;
constexpr uint32_t kOsThreadStackEndOffset = 0x398;
constexpr uint32_t kOsThreadEntryOffset = 0x39C;
constexpr uint32_t kOsThreadSpecificOffset = 0x57C;
constexpr uint32_t kOsThreadTypeOffset = 0x5BC;
constexpr uint32_t kOsThreadNameOffset = 0x5C0;
constexpr uint32_t kOsThreadTag = 0x74487244;
constexpr uint8_t kThreadReady = 1;
constexpr uint8_t kThreadRunning = 2;
constexpr uint8_t kThreadWaiting = 4;
constexpr uint8_t kThreadMoribund = 8;

constexpr uint32_t kMemHeapHeaderSize = 0x40;
constexpr uint32_t kHeapTagOffset = 0x00;
constexpr uint32_t kHeapDataStartOffset = 0x18;
constexpr uint32_t kHeapDataEndOffset = 0x1C;
constexpr uint32_t kHeapFlagsOffset = 0x30;
constexpr uint32_t kFrameHeapHeadOffset = 0x40;
constexpr uint32_t kFrameHeapTailOffset = 0x44;
constexpr uint32_t kFrameHeapPreviousStateOffset = 0x48;
constexpr uint32_t kFrameHeapSize = 0x4C;
constexpr uint32_t kExpandedHeapSize = 0x54;
constexpr uint32_t kExpandedHeapFreeListOffset = 0x40;
constexpr uint32_t kExpandedHeapUsedListOffset = 0x48;
constexpr uint32_t kExpandedHeapGroupOffset = 0x50;
constexpr uint32_t kExpandedHeapAttribsOffset = 0x52;
constexpr uint32_t kExpandedBlockSize = 0x14;
constexpr uint32_t kExpandedBlockAttribsOffset = 0x00;
constexpr uint32_t kExpandedBlockDataSizeOffset = 0x04;
constexpr uint32_t kExpandedBlockPrevOffset = 0x08;
constexpr uint32_t kExpandedBlockNextOffset = 0x0C;
constexpr uint32_t kExpandedBlockTagOffset = 0x10;
constexpr uint16_t kExpandedFreeTag = 0x4654;
constexpr uint16_t kExpandedUsedTag = 0x5544;
constexpr uint32_t kFrameHeapTag = 0x46524D48;
constexpr uint32_t kExpandedHeapTag = 0x45585048;

constexpr uint32_t kOsMutexSize = 0x2C;
constexpr uint32_t kMutexTagOffset = 0x00;
constexpr uint32_t kMutexQueueHeadOffset = 0x0C;
constexpr uint32_t kMutexQueueTailOffset = 0x10;
constexpr uint32_t kMutexOwnerOffset = 0x1C;
constexpr uint32_t kMutexRecursionOffset = 0x20;
constexpr uint32_t kOsMutexTag = 0x6D557458;
constexpr uint32_t kOsMessageQueueSize = 0x3C;
constexpr uint32_t kMessageQueueTagOffset = 0x00;
constexpr uint32_t kMessageQueueNameOffset = 0x04;
constexpr uint32_t kMessageQueueSendQueueOffset = 0x0C;
constexpr uint32_t kMessageQueueRecvQueueOffset = 0x1C;
constexpr uint32_t kMessageQueueMessagesOffset = 0x2C;
constexpr uint32_t kMessageQueueSizeOffset = 0x30;
constexpr uint32_t kMessageQueueFirstOffset = 0x34;
constexpr uint32_t kMessageQueueUsedOffset = 0x38;
constexpr uint32_t kOsThreadQueueSize = 0x10;
constexpr uint32_t kThreadQueueHeadOffset = 0x00;
constexpr uint32_t kThreadQueueTailOffset = 0x04;
constexpr uint32_t kThreadQueueParentOffset = 0x08;
constexpr uint32_t kOsMessageQueueTag = 0x6D536751;
constexpr uint32_t kOsMessageSize = 0x10;
constexpr uint32_t kMessageFlagBlocking = 0x1;
constexpr uint32_t kMessageFlagHighPriority = 0x2;
constexpr uint32_t kOsEventSize = 0x24;
constexpr uint32_t kOsEventTag = 0x65566E54;
constexpr uint32_t kEventNameOffset = 0x04;
constexpr uint32_t kEventValueOffset = 0x0C;
constexpr uint32_t kEventQueueOffset = 0x10;
constexpr uint32_t kEventModeOffset = 0x20;
constexpr uint32_t kOsAlarmSize = 0x58;
constexpr uint32_t kAlarmTagOffset = 0x00;
constexpr uint32_t kAlarmCallbackOffset = 0x0C;
constexpr uint32_t kAlarmNextFireOffset = 0x18;
constexpr uint32_t kAlarmPeriodOffset = 0x28;
constexpr uint32_t kAlarmUserDataOffset = 0x38;
constexpr uint32_t kAlarmStateOffset = 0x3C;
constexpr uint32_t kAlarmThreadQueueOffset = 0x40;
constexpr uint32_t kAlarmContextOffset = 0x54;
constexpr uint32_t kOsAlarmTag = 0x614C724D;


inline uint8_t read_u8(const GuestMemory& memory, uint32_t base,
                       uint32_t offset, uint32_t pc) {
    return memory.read8(base + offset, pc);
}

inline uint16_t read_u16(const GuestMemory& memory, uint32_t base,
                         uint32_t offset, uint32_t pc) {
    return memory.read16(base + offset, pc);
}

inline uint32_t read_u32(const GuestMemory& memory, uint32_t base,
                         uint32_t offset, uint32_t pc) {
    return memory.read32(base + offset, pc);
}

inline uint64_t read_u64(const GuestMemory& memory, uint32_t base,
                         uint32_t offset, uint32_t pc) {
    return memory.read64(base + offset, pc);
}

inline void write_u8(GuestMemory& memory, uint32_t base, uint32_t offset,
                     uint8_t value, uint32_t pc) {
    memory.write8(base + offset, value, pc);
}

inline void write_u16(GuestMemory& memory, uint32_t base, uint32_t offset,
                      uint16_t value, uint32_t pc) {
    memory.write16(base + offset, value, pc);
}

inline void write_u32(GuestMemory& memory, uint32_t base, uint32_t offset,
                      uint32_t value, uint32_t pc) {
    memory.write32(base + offset, value, pc);
}

inline void write_u64(GuestMemory& memory, uint32_t base, uint32_t offset,
                      uint64_t value, uint32_t pc) {
    memory.write64(base + offset, value, pc);
}
} // namespace nwii::runtime::abi
