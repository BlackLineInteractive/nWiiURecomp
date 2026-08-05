#include "runtime/cafe_coreinit.h"

#include "runtime/cafe_runtime.h"
#include "runtime/machine.h"

#include <algorithm>
#include <array>
#include <limits>
#include <fstream>
#include <stdexcept>
#include <iterator>
#include <string>

namespace nwii::runtime {
namespace {
constexpr uint32_t kErreulaHandle = 0x45525245;
constexpr uint32_t kSwkbdHandle = 0x53574B42;
constexpr uint32_t kErreulaStub = 0xC1000000;
constexpr uint32_t kYaz0Decompress = 0x0275F480;

template <size_t Size>
bool guest_string_equals(GuestMemory& memory, uint32_t address,
                         const char (&expected)[Size], uint32_t pc) {
    for (size_t index = 0; index < Size; ++index) {
        if (memory.read8(address + static_cast<uint32_t>(index), pc) !=
            static_cast<uint8_t>(expected[index])) {
            return false;
        }
    }
    return true;
}
} // namespace

CafeCoreinit::CafeCoreinit(
    ExecutionImage& image,
    std::optional<std::filesystem::path> shared_font)
    : image_(image), memory_(image.memory),
      frame_heaps_{{abi::kMem1Heap,
                    {abi::kMem1Heap, abi::kMem1Start, abi::kMem1End,
                     abi::kMem1Start, abi::kMem1End, 0}},
                   {abi::kFgHeap,
                    {abi::kFgHeap, abi::kFgStart, abi::kFgEnd,
                     abi::kFgStart, abi::kFgEnd, 0}}},
      expanded_heaps_{{abi::kMem2Heap,
                       {abi::kMem2Heap,
                        abi::kMem2Start,
                        abi::kMem2End,
                        {{abi::kMem2Start,
                          abi::kMem2End - abi::kMem2Start}},
                        {},
                        0,
                        false}}} {
    constexpr std::array ranges{
        std::pair{abi::kCafeSystemStart, abi::kCafeSystemEnd},
        std::pair{abi::kMem2Heap, abi::kMem2End},
        std::pair{abi::kFgHeap, abi::kFgEnd},
        std::pair{abi::kMem1Heap, abi::kMem1End},
    };
    try {
        for (const auto& [start, end] : ranges) {
            memory_.map(start, end - start, {true, true, false});
        }
    } catch (const std::runtime_error& error) {
        throw std::runtime_error(std::string{"Cafe memory collision: "} +
                                 error.what());
    }

    for (const auto& [handle, heap] : frame_heaps_) {
        abi::write_u32(memory_, handle, abi::kHeapTagOffset,
                       abi::kFrameHeapTag, 0);
        abi::write_u32(memory_, handle, abi::kHeapDataStartOffset, heap.start,
                       0);
        abi::write_u32(memory_, handle, abi::kHeapDataEndOffset, heap.end, 0);
        abi::write_u32(memory_, handle, abi::kHeapFlagsOffset, heap.flags, 0);
        mirror_frame(heap);
    }
    for (const auto& [handle, heap] : expanded_heaps_) {
        abi::write_u32(memory_, handle, abi::kHeapTagOffset,
                       abi::kExpandedHeapTag, 0);
        abi::write_u32(memory_, handle, abi::kHeapDataStartOffset, heap.start,
                       0);
        abi::write_u32(memory_, handle, abi::kHeapDataEndOffset, heap.end, 0);
        abi::write_u32(memory_, handle, abi::kHeapFlagsOffset, heap.flags, 0);
    }
    init_mutex(abi::kGhsMutex, 0);
    abi::write_u32(memory_, abi::kOsSystemInfo, 0, abi::kBusClockSpeed, 0);
    abi::write_u32(memory_, abi::kOsSystemInfo, 4, abi::kCoreClockSpeed, 0);
    abi::write_u64(memory_, abi::kOsSystemInfo, 8, 0, 0);
    abi::write_u32(memory_, abi::kOsSystemInfo, 0x10, 512 * 1024, 0);
    abi::write_u32(memory_, abi::kOsSystemInfo, 0x14, 2 * 1024 * 1024, 0);
    abi::write_u32(memory_, abi::kOsSystemInfo, 0x18, 512 * 1024, 0);
    abi::write_u32(memory_, abi::kOsSystemInfo, 0x1C, 5, 0);
    for (const auto& [address, import] : image_.imports) {
        if (import.module == "coreinit" && import.symbol == "_iob") {
            constexpr uint32_t imported_iob_prefix = 0x20;
            // ponytail: startup uses three FILE entries; fully relocate data
            // imports before supporting the complete GHS stream table.
            memory_.map(address + imported_iob_prefix, 0x30,
                        {true, true, false});
            memory_.patch32(address, abi::kGhsIob);
        }
    }
    if (shared_font) {
        std::ifstream input(*shared_font, std::ios::binary);
        if (!input) {
            throw std::invalid_argument("cannot open shared font: " +
                                        shared_font->string());
        }
        shared_font_.assign(std::istreambuf_iterator<char>{input},
                            std::istreambuf_iterator<char>{});
        if (shared_font_.empty() ||
            shared_font_.size() > std::numeric_limits<uint32_t>::max()) {
            throw std::invalid_argument("invalid shared font: " +
                                        shared_font->string());
        }
    }
}

void CafeCoreinit::attach_machine(Machine& machine) { machine_ = &machine; }

[[noreturn]] void CafeCoreinit::fault(const char* reason, uint32_t address,
                                      uint32_t width, uint32_t pc,
                                      MemoryAccess access) const {
    throw GuestFault(reason, address, width, pc, access);
}

bool& CafeCoreinit::interrupt_state(uint32_t pc) {
    if (machine_ == nullptr || machine_->current_core_id() >= 3) {
        fault("interrupt state requires Cafe core", pc, 4, pc);
    }
    return interrupts_enabled_[machine_->current_core_id()];
}

uint32_t CafeCoreinit::alignment_magnitude(int32_t alignment, uint32_t pc,
                                           uint32_t address) {
    if (alignment == 0 || alignment == std::numeric_limits<int32_t>::min()) {
        throw GuestFault("invalid heap alignment", address, 4, pc,
                         MemoryAccess::read);
    }
    const uint32_t magnitude = static_cast<uint32_t>(
        alignment < 0 ? -alignment : alignment);
    if ((magnitude & (magnitude - 1)) != 0) {
        throw GuestFault("invalid heap alignment", address, 4, pc,
                         MemoryAccess::read);
    }
    return magnitude;
}

uint32_t CafeCoreinit::align_up(uint32_t value, uint32_t alignment,
                                uint32_t pc, uint32_t address) {
    const uint32_t mask = alignment - 1;
    if (value > std::numeric_limits<uint32_t>::max() - mask) {
        throw GuestFault("heap alignment overflow", address, 4, pc,
                         MemoryAccess::read);
    }
    return (value + mask) & ~mask;
}

uint32_t CafeCoreinit::align_down(uint32_t value, uint32_t alignment) {
    return value & ~(alignment - 1);
}

void CafeCoreinit::validate_range(uint32_t address, uint32_t size, uint32_t pc,
                                  MemoryAccess access) const {
    memory_.validate(address, size, pc, access);
}

uint32_t CafeCoreinit::base_heap(uint32_t type, uint32_t pc) const {
    switch (type) {
    case 0:
        return abi::kMem1Heap;
    case 1:
        return abi::kMem2Heap;
    case 8:
        return abi::kFgHeap;
    default:
        fault("invalid base heap", type, 4, pc);
    }
}

CafeCoreinit::FrameHeap& CafeCoreinit::frame_heap(uint32_t handle,
                                                  uint32_t pc) {
    return const_cast<FrameHeap&>(
        static_cast<const CafeCoreinit&>(*this).frame_heap(handle, pc));
}

const CafeCoreinit::FrameHeap& CafeCoreinit::frame_heap(uint32_t handle,
                                                        uint32_t pc) const {
    const auto found = frame_heaps_.find(handle);
    if (found == frame_heaps_.end() ||
        abi::read_u32(memory_, handle, abi::kHeapTagOffset, pc) !=
            abi::kFrameHeapTag) {
        fault("invalid frame heap", handle, abi::kFrameHeapSize, pc);
    }
    return found->second;
}

CafeCoreinit::ExpandedHeap& CafeCoreinit::expanded_heap(uint32_t handle,
                                                        uint32_t pc) {
    return const_cast<ExpandedHeap&>(
        static_cast<const CafeCoreinit&>(*this).expanded_heap(handle, pc));
}

const CafeCoreinit::ExpandedHeap& CafeCoreinit::expanded_heap(
    uint32_t handle, uint32_t pc) const {
    const auto found = expanded_heaps_.find(handle);
    if (found == expanded_heaps_.end() ||
        abi::read_u32(memory_, handle, abi::kHeapTagOffset, pc) !=
            abi::kExpandedHeapTag) {
        fault("invalid expanded heap", handle, abi::kExpandedHeapSize, pc);
    }
    return found->second;
}

void CafeCoreinit::mirror_frame(const FrameHeap& heap) {
    abi::write_u32(memory_, heap.handle, abi::kFrameHeapHeadOffset, heap.head,
                   0);
    abi::write_u32(memory_, heap.handle, abi::kFrameHeapTailOffset, heap.tail,
                   0);
}

void CafeCoreinit::mirror_expanded(const ExpandedHeap& heap, uint32_t pc) {
    if (!heap.mirror_blocks) {
        return;
    }
    const uint32_t free_head =
        heap.free.empty() ? 0 : heap.free.front().address;
    const uint32_t free_tail =
        heap.free.empty() ? 0 : heap.free.back().address;
    abi::write_u32(memory_, heap.handle, abi::kExpandedHeapFreeListOffset,
                   free_head, pc);
    abi::write_u32(memory_, heap.handle,
                   abi::kExpandedHeapFreeListOffset + 4, free_tail, pc);

    for (size_t index = 0; index < heap.free.size(); ++index) {
        const auto& block = heap.free[index];
        abi::write_u32(memory_, block.address,
                       abi::kExpandedBlockAttribsOffset, 0, pc);
        abi::write_u32(memory_, block.address,
                       abi::kExpandedBlockDataSizeOffset,
                       block.size - abi::kExpandedBlockSize, pc);
        abi::write_u32(memory_, block.address,
                       abi::kExpandedBlockPrevOffset,
                       index == 0 ? 0 : heap.free[index - 1].address, pc);
        abi::write_u32(
            memory_, block.address, abi::kExpandedBlockNextOffset,
            index + 1 == heap.free.size() ? 0 : heap.free[index + 1].address,
            pc);
        abi::write_u16(memory_, block.address, abi::kExpandedBlockTagOffset,
                       abi::kExpandedFreeTag, pc);
    }

    const uint32_t used_head =
        heap.allocated.empty()
            ? 0
            : heap.allocated.begin()->first - abi::kExpandedBlockSize;
    const uint32_t used_tail =
        heap.allocated.empty()
            ? 0
            : heap.allocated.rbegin()->first - abi::kExpandedBlockSize;
    abi::write_u32(memory_, heap.handle, abi::kExpandedHeapUsedListOffset,
                   used_head, pc);
    abi::write_u32(memory_, heap.handle,
                   abi::kExpandedHeapUsedListOffset + 4, used_tail, pc);

    uint32_t previous = 0;
    for (auto allocation = heap.allocated.begin();
         allocation != heap.allocated.end(); ++allocation) {
        auto next = allocation;
        ++next;
        const uint32_t block =
            allocation->first - abi::kExpandedBlockSize;
        abi::write_u32(
            memory_, block, abi::kExpandedBlockAttribsOffset,
            allocation->second.from_end ? uint32_t{1} << 31 : 0, pc);
        abi::write_u32(memory_, block, abi::kExpandedBlockDataSizeOffset,
                       allocation->second.size, pc);
        abi::write_u32(memory_, block, abi::kExpandedBlockPrevOffset,
                       previous, pc);
        abi::write_u32(
            memory_, block, abi::kExpandedBlockNextOffset,
            next == heap.allocated.end()
                ? 0
                : next->first - abi::kExpandedBlockSize,
            pc);
        abi::write_u16(memory_, block, abi::kExpandedBlockTagOffset,
                       abi::kExpandedUsedTag, pc);
        previous = block;
    }
}

uint32_t CafeCoreinit::create_expanded_heap(uint32_t base, uint32_t size,
                                            uint32_t flags, uint32_t pc) {
    constexpr uint32_t alignment = 4;
    if (base == 0 ||
        base > std::numeric_limits<uint32_t>::max() - (alignment - 1) ||
        size > std::numeric_limits<uint32_t>::max() - base) {
        return 0;
    }
    const uint32_t handle = (base + alignment - 1) & ~(alignment - 1);
    const uint32_t end = (base + size) & ~(alignment - 1);
    if (end <= handle ||
        end - handle <
            abi::kExpandedHeapSize + abi::kExpandedBlockSize + 4 ||
        expanded_heaps_.contains(handle)) {
        return 0;
    }
    validate_range(handle, end - handle, pc, MemoryAccess::write);

    const uint32_t start = handle + abi::kExpandedHeapSize;
    const auto [found, inserted] = expanded_heaps_.try_emplace(
        handle, ExpandedHeap{handle,
                             start,
                             end,
                             {{start, end - start}},
                             {},
                             flags,
                             true});
    if (!inserted) {
        return 0;
    }
    for (uint32_t offset = 0; offset < abi::kExpandedHeapSize; offset += 4) {
        abi::write_u32(memory_, handle, offset, 0, pc);
    }
    abi::write_u32(memory_, handle, abi::kHeapTagOffset,
                   abi::kExpandedHeapTag, pc);
    abi::write_u32(memory_, handle, abi::kHeapDataStartOffset, start, pc);
    abi::write_u32(memory_, handle, abi::kHeapDataEndOffset, end, pc);
    abi::write_u32(memory_, handle, abi::kHeapFlagsOffset, flags, pc);
    mirror_expanded(found->second, pc);
    return handle;
}

uint32_t CafeCoreinit::destroy_expanded_heap(uint32_t handle, uint32_t pc) {
    const auto& heap = expanded_heap(handle, pc);
    if (!heap.allocated.empty()) {
        fault("expanded heap has live allocations", handle,
              abi::kExpandedHeapSize, pc);
    }
    abi::write_u32(memory_, handle, abi::kHeapTagOffset, 0, pc);
    expanded_heaps_.erase(handle);
    return handle;
}

uint32_t CafeCoreinit::create_frame_heap(uint32_t base, uint32_t size,
                                         uint32_t flags, uint32_t pc) {
    constexpr uint32_t alignment = 4;
    if (base == 0 ||
        base > std::numeric_limits<uint32_t>::max() - (alignment - 1) ||
        size > std::numeric_limits<uint32_t>::max() - base) {
        return 0;
    }
    const uint32_t handle = (base + alignment - 1) & ~(alignment - 1);
    const uint32_t end = (base + size) & ~(alignment - 1);
    if (end <= handle || end - handle < abi::kFrameHeapSize ||
        frame_heaps_.contains(handle)) {
        return 0;
    }
    validate_range(handle, end - handle, pc, MemoryAccess::write);

    const auto [found, inserted] = frame_heaps_.try_emplace(
        handle, FrameHeap{handle, handle + abi::kFrameHeapSize, end,
                          handle + abi::kFrameHeapSize, end, flags});
    if (!inserted) {
        return 0;
    }
    for (uint32_t offset = 0; offset < abi::kFrameHeapSize; offset += 4) {
        abi::write_u32(memory_, handle, offset, 0, pc);
    }
    abi::write_u32(memory_, handle, abi::kHeapTagOffset, abi::kFrameHeapTag,
                   pc);
    abi::write_u32(memory_, handle, abi::kHeapDataStartOffset,
                   found->second.start, pc);
    abi::write_u32(memory_, handle, abi::kHeapDataEndOffset,
                   found->second.end, pc);
    abi::write_u32(memory_, handle, abi::kHeapFlagsOffset,
                   found->second.flags, pc);
    mirror_frame(found->second);
    return handle;
}

uint32_t CafeCoreinit::destroy_frame_heap(uint32_t handle, uint32_t pc) {
    static_cast<void>(frame_heap(handle, pc));
    abi::write_u32(memory_, handle, abi::kHeapTagOffset, 0, pc);
    frame_heaps_.erase(handle);
    return handle;
}

uint32_t CafeCoreinit::allocate_frame(uint32_t handle, uint32_t size,
                                      int32_t alignment, uint32_t pc) {
    auto& heap = frame_heap(handle, pc);
    const uint32_t magnitude = alignment_magnitude(alignment, pc, handle);
    if (size == 0) {
        return 0;
    }

    uint32_t result;
    if (alignment > 0) {
        result = align_up(heap.head, magnitude, pc, handle);
        if (result > heap.tail || size > heap.tail - result) {
            return 0;
        }
        heap.head = result + size;
    } else {
        if (size > heap.tail - heap.head) {
            return 0;
        }
        result = align_down(heap.tail - size, magnitude);
        if (result < heap.head) {
            return 0;
        }
        heap.tail = result;
    }
    mirror_frame(heap);
    return result;
}

uint32_t CafeCoreinit::frame_allocatable(uint32_t handle, int32_t alignment,
                                         uint32_t pc) const {
    const auto& heap = frame_heap(handle, pc);
    const uint32_t magnitude = alignment_magnitude(alignment, pc, handle);
    const uint32_t begin =
        align_up(heap.head, magnitude, pc, handle);
    const uint32_t end = heap.tail;
    return end > begin ? end - begin : 0;
}

uint32_t CafeCoreinit::allocate_expanded(uint32_t handle, uint32_t size,
                                         int32_t alignment, uint32_t pc) {
    auto& heap = expanded_heap(handle, pc);
    uint32_t magnitude = alignment_magnitude(alignment, pc, handle);
    const uint32_t header =
        heap.mirror_blocks ? abi::kExpandedBlockSize : 0;
    const uint32_t minimum_split =
        heap.mirror_blocks ? abi::kExpandedBlockSize + 4 : 1;
    if (heap.mirror_blocks) {
        magnitude = std::max(magnitude, uint32_t{4});
        if (size == 0) {
            size = 1;
        }
        if (size > std::numeric_limits<uint32_t>::max() - 3) {
            return 0;
        }
        size = (size + 3) & ~uint32_t{3};
    } else if (size == 0) {
        return 0;
    }

    for (size_t step = 0; step < heap.free.size(); ++step) {
        const size_t index = alignment > 0 ? step : heap.free.size() - 1 - step;
        const FreeBlock block = heap.free[index];
        const uint32_t block_end = block.address + block.size;
        if (block.size <= header) {
            continue;
        }

        uint32_t result;
        if (alignment > 0) {
            result = align_up(block.address + header, magnitude, pc, handle);
            if (result > block_end || size > block_end - result) {
                continue;
            }
        } else {
            if (size > block.size - header) {
                continue;
            }
            result = align_down(block_end - size, magnitude);
            if (result < block.address + header) {
                continue;
            }
        }

        uint32_t allocation_start = result - header;
        uint32_t allocation_end = result + size;
        const uint32_t prefix = allocation_start - block.address;
        const uint32_t suffix = block_end - allocation_end;
        if (prefix != 0 && prefix < minimum_split) {
            allocation_start = block.address;
        }
        if (suffix != 0 && suffix < minimum_split) {
            allocation_end = block_end;
        }

        heap.free.erase(heap.free.begin() + static_cast<ptrdiff_t>(index));
        size_t insert = index;
        if (allocation_start > block.address) {
            heap.free.insert(
                heap.free.begin() + static_cast<ptrdiff_t>(insert),
                {block.address, allocation_start - block.address});
            ++insert;
        }
        if (allocation_end < block_end) {
            heap.free.insert(
                heap.free.begin() + static_cast<ptrdiff_t>(insert),
                {allocation_end, block_end - allocation_end});
        }
        heap.allocated.emplace(
            result,
            ExpandedAllocation{allocation_start,
                               allocation_end - allocation_start,
                               allocation_end - result, alignment < 0});
        mirror_expanded(heap, pc);
        return result;
    }
    return 0;
}

void CafeCoreinit::free_expanded(uint32_t handle, uint32_t address,
                                 uint32_t pc) {
    auto& heap = expanded_heap(handle, pc);
    const auto allocation = heap.allocated.find(address);
    if (allocation == heap.allocated.end()) {
        fault("invalid expanded allocation", address, 4, pc);
    }
    const FreeBlock released{allocation->second.raw_address,
                             allocation->second.raw_size};
    heap.allocated.erase(allocation);
    const auto position = std::lower_bound(
        heap.free.begin(), heap.free.end(), released.address,
        [](const FreeBlock& block, uint32_t value) {
            return block.address < value;
        });
    size_t index = static_cast<size_t>(position - heap.free.begin());
    heap.free.insert(position, released);
    if (index > 0 &&
        heap.free[index - 1].address + heap.free[index - 1].size ==
            heap.free[index].address) {
        heap.free[index - 1].size += heap.free[index].size;
        heap.free.erase(heap.free.begin() + static_cast<ptrdiff_t>(index));
        --index;
    }
    if (index + 1 < heap.free.size() &&
        heap.free[index].address + heap.free[index].size ==
            heap.free[index + 1].address) {
        heap.free[index].size += heap.free[index + 1].size;
        heap.free.erase(heap.free.begin() +
                        static_cast<ptrdiff_t>(index + 1));
    }
    mirror_expanded(heap, pc);
}

uint32_t CafeCoreinit::expanded_allocatable(uint32_t handle,
                                            int32_t alignment,
                                            uint32_t pc) const {
    const auto& heap = expanded_heap(handle, pc);
    uint32_t magnitude = alignment_magnitude(alignment, pc, handle);
    const uint32_t header =
        heap.mirror_blocks ? abi::kExpandedBlockSize : 0;
    if (heap.mirror_blocks) {
        magnitude = std::max(magnitude, uint32_t{4});
    }
    uint32_t largest = 0;
    for (const auto& block : heap.free) {
        if (block.size <= header) {
            continue;
        }
        const uint32_t begin =
            align_up(block.address + header, magnitude, pc, handle);
        const uint32_t end = block.address + block.size;
        if (end > begin) {
            largest = std::max(largest, end - begin);
        }
    }
    return largest;
}

uint32_t CafeCoreinit::expanded_total_free(uint32_t handle,
                                           uint32_t pc) const {
    const auto& heap = expanded_heap(handle, pc);
    const uint32_t header =
        heap.mirror_blocks ? abi::kExpandedBlockSize : 0;
    uint64_t total = 0;
    for (const auto& block : heap.free) {
        if (block.size > header) {
            total += block.size - header;
        }
    }
    return static_cast<uint32_t>(total);
}

void CafeCoreinit::initialize_thread(uint32_t address, uint16_t id,
                                     int32_t priority, uint32_t stack_start,
                                     uint32_t stack_end) {
    validate_range(address, abi::kOsThreadSize, 0, MemoryAccess::write);
    abi::write_u64(memory_, address, abi::kOsContextTagOffset,
                   abi::kOsContextTag, 0);
    abi::write_u32(memory_, address, abi::kOsThreadTagOffset,
                   abi::kOsThreadTag, 0);
    abi::write_u8(memory_, address, abi::kOsThreadStateOffset,
                  abi::kThreadReady, 0);
    abi::write_u8(memory_, address, abi::kOsThreadAttrOffset, 7, 0);
    abi::write_u16(memory_, address, abi::kOsThreadIdOffset, id, 0);
    abi::write_u32(memory_, address, abi::kOsThreadPriorityOffset,
                   static_cast<uint32_t>(priority + 64), 0);
    abi::write_u32(memory_, address, abi::kOsThreadBasePriorityOffset,
                   static_cast<uint32_t>(priority + 64), 0);
    abi::write_u32(memory_, address, abi::kOsThreadStackStartOffset,
                   stack_start, 0);
    abi::write_u32(memory_, address, abi::kOsThreadStackEndOffset, stack_end,
                   0);
    abi::write_u32(memory_, address, abi::kOsThreadTypeOffset, 2, 0);
}

void CafeCoreinit::set_thread_state(uint32_t address, uint8_t state) {
    abi::write_u8(memory_, address, abi::kOsThreadStateOffset, state, 0);
}

void CafeCoreinit::init_mutex(uint32_t address, uint32_t pc) {
    if ((address & 3) != 0) {
        fault("invalid mutex alignment", address, abi::kOsMutexSize, pc);
    }
    validate_range(address, abi::kOsMutexSize, pc, MemoryAccess::write);
    for (uint32_t offset = 0; offset < abi::kOsMutexSize; ++offset) {
        memory_.write8(address + offset, 0, pc);
    }
    abi::write_u32(memory_, address, abi::kMutexTagOffset, abi::kOsMutexTag,
                   pc);
    mutexes_.insert_or_assign(address, MutexState{});
}

CafeCoreinit::MutexState& CafeCoreinit::mutex(uint32_t address, uint32_t pc) {
    if ((address & 3) != 0) {
        fault("invalid mutex alignment", address, abi::kOsMutexSize, pc);
    }
    validate_range(address, abi::kOsMutexSize, pc, MemoryAccess::read);
    const auto found = mutexes_.find(address);
    if (found == mutexes_.end() ||
        abi::read_u32(memory_, address, abi::kMutexTagOffset, pc) !=
            abi::kOsMutexTag) {
        fault("invalid mutex", address, abi::kOsMutexSize, pc);
    }
    return found->second;
}

bool CafeCoreinit::waiter_before(const MutexWaiter& left,
                                 const MutexWaiter& right) {
    return left.priority < right.priority ||
           (left.priority == right.priority &&
            left.sequence < right.sequence);
}

void CafeCoreinit::mirror_thread_queue(
    uint32_t base, uint32_t head_offset,
    const std::vector<MutexWaiter>& waiters, uint32_t pc) {
    uint32_t head = 0;
    uint32_t tail = 0;
    if (!waiters.empty()) {
        head = std::min_element(waiters.begin(), waiters.end(),
                                waiter_before)
                   ->thread;
        tail = std::max_element(waiters.begin(), waiters.end(),
                                waiter_before)
                   ->thread;
    }
    abi::write_u32(memory_, base, head_offset, head, pc);
    abi::write_u32(memory_, base, head_offset + 4, tail, pc);
}

void CafeCoreinit::mirror_mutex(uint32_t address, const MutexState& state,
                                uint32_t pc) {
    mirror_thread_queue(address, abi::kMutexQueueHeadOffset, state.waiters,
                        pc);
    abi::write_u32(memory_, address, abi::kMutexOwnerOffset, state.owner, pc);
    abi::write_u32(memory_, address, abi::kMutexRecursionOffset,
                   state.recursion, pc);
}

HleAction CafeCoreinit::lock_mutex(uint32_t address, CPUContext& cpu) {
    auto& state = mutex(address, cpu.pc);
    if (machine_ == nullptr) {
        fault("mutex requires Cafe scheduler", address, abi::kOsMutexSize,
              cpu.pc);
    }
    const uint32_t current = machine_->current_thread_address();
    if (state.owner == 0) {
        state.owner = current;
        state.recursion = 1;
        state.granted = 0;
        mirror_mutex(address, state, cpu.pc);
        return HleAction::return_to_lr;
    }
    if (state.owner == current) {
        if (state.granted == current) {
            state.granted = 0;
        } else {
            if (state.recursion == std::numeric_limits<uint32_t>::max()) {
                fault("mutex recursion overflow", address,
                      abi::kOsMutexSize, cpu.pc);
            }
            ++state.recursion;
        }
        mirror_mutex(address, state, cpu.pc);
        return HleAction::return_to_lr;
    }

    const auto waiting = std::find_if(
        state.waiters.begin(), state.waiters.end(),
        [current](const MutexWaiter& waiter) {
            return waiter.thread == current;
        });
    if (waiting == state.waiters.end()) {
        state.waiters.push_back(
            {current, machine_->current_thread_priority(),
             wait_sequence_++});
    }
    mirror_mutex(address, state, cpu.pc);
    machine_->block_current(address);
    return HleAction::reschedule;
}

HleAction CafeCoreinit::try_lock_mutex(uint32_t address, CPUContext& cpu) {
    auto& state = mutex(address, cpu.pc);
    if (machine_ == nullptr) {
        fault("mutex requires Cafe scheduler", address, abi::kOsMutexSize,
              cpu.pc);
    }
    const uint32_t current = machine_->current_thread_address();
    if (state.owner != 0 && state.owner != current) {
        cpu.gpr[3] = 0;
        return HleAction::return_to_lr;
    }
    if (state.owner == current &&
        state.recursion == std::numeric_limits<uint32_t>::max()) {
        fault("mutex recursion overflow", address, abi::kOsMutexSize, cpu.pc);
    }
    validate_range(address, abi::kOsMutexSize, cpu.pc, MemoryAccess::write);
    if (state.owner == 0) {
        state.owner = current;
        state.recursion = 1;
        state.granted = 0;
    } else {
        ++state.recursion;
    }
    mirror_mutex(address, state, cpu.pc);
    cpu.gpr[3] = 1;
    return HleAction::return_to_lr;
}

HleAction CafeCoreinit::unlock_mutex(uint32_t address, CPUContext& cpu) {
    auto& state = mutex(address, cpu.pc);
    if (machine_ == nullptr ||
        state.owner != machine_->current_thread_address()) {
        fault("mutex unlock by non-owner", address, abi::kOsMutexSize,
              cpu.pc);
    }
    if (state.recursion > 1) {
        --state.recursion;
        mirror_mutex(address, state, cpu.pc);
        return HleAction::return_to_lr;
    }
    if (state.waiters.empty()) {
        state.owner = 0;
        state.recursion = 0;
        state.granted = 0;
        mirror_mutex(address, state, cpu.pc);
        return HleAction::return_to_lr;
    }

    const auto next = std::min_element(state.waiters.begin(),
                                       state.waiters.end(), waiter_before);
    const uint32_t woken = next->thread;
    state.waiters.erase(next);
    state.owner = woken;
    state.recursion = 1;
    state.granted = woken;
    mirror_mutex(address, state, cpu.pc);
    machine_->wake_thread(woken);
    return HleAction::return_to_lr;
}

uint32_t CafeCoreinit::pointer_to_mutex(CPUContext& cpu,
                                        GuestMemory& memory) const {
    const uint32_t pointer = cpu.gpr[3];
    if ((pointer & 3) != 0) {
        fault("invalid GHS mutex pointer alignment", pointer, 4, cpu.pc);
    }
    validate_range(pointer, 4, cpu.pc, MemoryAccess::read);
    return memory.read32(pointer, cpu.pc);
}

CafeCoreinit::MessageQueueState& CafeCoreinit::message_queue(uint32_t address,
                                                             uint32_t pc) {
    validate_range(address, abi::kOsMessageQueueSize, pc, MemoryAccess::read);
    const auto found = message_queues_.find(address);
    if (found == message_queues_.end() ||
        abi::read_u32(memory_, address, abi::kMessageQueueTagOffset, pc) !=
            abi::kOsMessageQueueTag) {
        fault("invalid message queue", address, abi::kOsMessageQueueSize, pc);
    }
    return found->second;
}

CafeCoreinit::EventState& CafeCoreinit::event(uint32_t address, uint32_t pc) {
    validate_range(address, abi::kOsEventSize, pc, MemoryAccess::read);
    const auto found = events_.find(address);
    if (found == events_.end() ||
        abi::read_u32(memory_, address, 0, pc) != abi::kOsEventTag) {
        fault("invalid event", address, abi::kOsEventSize, pc);
    }
    return found->second;
}

HleAction CafeCoreinit::wait_event(CPUContext& cpu) {
    const uint32_t address = cpu.gpr[3];
    auto& state = event(address, cpu.pc);
    if (machine_ == nullptr) {
        fault("event requires Cafe scheduler", address, abi::kOsEventSize,
              cpu.pc);
    }
    validate_range(address, abi::kOsEventSize, cpu.pc, MemoryAccess::write);
    const uint32_t mode =
        abi::read_u32(memory_, address, abi::kEventModeOffset, cpu.pc);
    if (mode > 1) {
        fault("invalid event mode", address, abi::kOsEventSize, cpu.pc);
    }
    const uint32_t current = machine_->current_thread_address();
    const auto granted =
        std::find(state.granted.begin(), state.granted.end(), current);
    if (granted != state.granted.end()) {
        state.granted.erase(granted);
        return HleAction::return_to_lr;
    }
    if (abi::read_u32(memory_, address, abi::kEventValueOffset, cpu.pc) != 0) {
        if (mode == 1) {
            abi::write_u32(memory_, address, abi::kEventValueOffset, 0,
                           cpu.pc);
        }
        return HleAction::return_to_lr;
    }
    if (std::none_of(
            state.waiters.begin(), state.waiters.end(),
            [current](const MutexWaiter& waiter) {
                return waiter.thread == current;
            })) {
        state.waiters.push_back(
            {current, machine_->current_thread_priority(), wait_sequence_++});
        mirror_thread_queue(address, abi::kEventQueueOffset, state.waiters,
                            cpu.pc);
    }
    machine_->block_current(address);
    return HleAction::reschedule;
}

HleAction CafeCoreinit::signal_event(CPUContext& cpu, bool all) {
    const uint32_t address = cpu.gpr[3];
    auto& state = event(address, cpu.pc);
    if (machine_ == nullptr) {
        fault("event requires Cafe scheduler", address, abi::kOsEventSize,
              cpu.pc);
    }
    validate_range(address, abi::kOsEventSize, cpu.pc, MemoryAccess::write);
    const uint32_t mode =
        abi::read_u32(memory_, address, abi::kEventModeOffset, cpu.pc);
    if (mode > 1) {
        fault("invalid event mode", address, abi::kOsEventSize, cpu.pc);
    }
    if (abi::read_u32(memory_, address, abi::kEventValueOffset, cpu.pc) != 0) {
        return HleAction::return_to_lr;
    }
    if (state.waiters.empty()) {
        abi::write_u32(memory_, address, abi::kEventValueOffset, 1, cpu.pc);
        return HleAction::return_to_lr;
    }

    if (mode == 1 && !all) {
        const auto next = std::min_element(state.waiters.begin(),
                                           state.waiters.end(), waiter_before);
        const uint32_t thread = next->thread;
        state.waiters.erase(next);
        state.granted.push_back(thread);
        mirror_thread_queue(address, abi::kEventQueueOffset, state.waiters,
                            cpu.pc);
        machine_->wake_thread(thread);
    } else {
        if (mode == 0) {
            abi::write_u32(memory_, address, abi::kEventValueOffset, 1,
                           cpu.pc);
        }
        for (const auto& waiter : state.waiters) {
            if (mode == 1) {
                state.granted.push_back(waiter.thread);
            }
            machine_->wake_thread(waiter.thread);
        }
        state.waiters.clear();
        mirror_thread_queue(address, abi::kEventQueueOffset, state.waiters,
                            cpu.pc);
    }
    cpu.pc = cpu.lr;
    return HleAction::reschedule;
}


HleAction CafeCoreinit::send_message(CPUContext& cpu) {
    const uint32_t queue = cpu.gpr[3];
    const uint32_t message = cpu.gpr[4];
    const uint32_t flags = cpu.gpr[5];
    auto& state = message_queue(queue, cpu.pc);
    if (machine_ == nullptr) {
        fault("message queue requires Cafe scheduler", queue,
              abi::kOsMessageQueueSize, cpu.pc);
    }
    validate_range(queue, abi::kOsMessageQueueSize, cpu.pc,
                   MemoryAccess::write);
    const uint32_t messages = abi::read_u32(
        memory_, queue, abi::kMessageQueueMessagesOffset, cpu.pc);
    const uint32_t size =
        abi::read_u32(memory_, queue, abi::kMessageQueueSizeOffset, cpu.pc);
    const uint32_t first =
        abi::read_u32(memory_, queue, abi::kMessageQueueFirstOffset, cpu.pc);
    const uint32_t used =
        abi::read_u32(memory_, queue, abi::kMessageQueueUsedOffset, cpu.pc);
    if (used > size || (size != 0 && first >= size)) {
        fault("corrupt message queue ring", queue, abi::kOsMessageQueueSize,
              cpu.pc);
    }
    const uint64_t buffer_bytes = uint64_t{size} * abi::kOsMessageSize;
    if (buffer_bytes > std::numeric_limits<uint32_t>::max()) {
        fault("message buffer overflow", messages, abi::kOsMessageSize,
              cpu.pc, MemoryAccess::write);
    }
    memory_.validate_range(messages, static_cast<uint32_t>(buffer_bytes),
                           cpu.pc, MemoryAccess::write);
    memory_.validate_range(message, abi::kOsMessageSize, cpu.pc,
                           MemoryAccess::read);

    const uint32_t current = machine_->current_thread_address();
    const auto self = std::find_if(
        state.send_waiters.begin(), state.send_waiters.end(),
        [current](const MutexWaiter& waiter) {
            return waiter.thread == current;
        });
    if (used == size) {
        if ((flags & abi::kMessageFlagBlocking) == 0) {
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        }
        if (self == state.send_waiters.end()) {
            state.send_waiters.push_back(
                {current, machine_->current_thread_priority(),
                 wait_sequence_++});
            mirror_thread_queue(queue, abi::kMessageQueueSendQueueOffset,
                                state.send_waiters, cpu.pc);
        }
        machine_->block_current(queue);
        return HleAction::reschedule;
    }
    if (self != state.send_waiters.end()) {
        state.send_waiters.erase(self);
        mirror_thread_queue(queue, abi::kMessageQueueSendQueueOffset,
                            state.send_waiters, cpu.pc);
    }

    uint32_t slot;
    if ((flags & abi::kMessageFlagHighPriority) != 0) {
        slot = first == 0 ? size - 1 : first - 1;
        abi::write_u32(memory_, queue, abi::kMessageQueueFirstOffset, slot,
                       cpu.pc);
    } else {
        slot = (first + used) % size;
    }
    const uint32_t destination = messages + slot * abi::kOsMessageSize;
    for (uint32_t offset = 0; offset < abi::kOsMessageSize; ++offset) {
        memory_.write8(destination + offset,
                       memory_.read8(message + offset, cpu.pc), cpu.pc);
    }
    abi::write_u32(memory_, queue, abi::kMessageQueueUsedOffset, used + 1,
                   cpu.pc);
    cpu.gpr[3] = 1;
    if (!state.recv_waiters.empty()) {
        const auto woken = std::min_element(state.recv_waiters.begin(),
                                            state.recv_waiters.end(),
                                            waiter_before);
        const uint32_t thread = woken->thread;
        state.recv_waiters.erase(woken);
        mirror_thread_queue(queue, abi::kMessageQueueRecvQueueOffset,
                            state.recv_waiters, cpu.pc);
        machine_->wake_thread(thread);
        cpu.pc = cpu.lr;
        return HleAction::reschedule;
    }
    return HleAction::return_to_lr;
}

HleAction CafeCoreinit::receive_message(CPUContext& cpu) {
    const uint32_t queue = cpu.gpr[3];
    const uint32_t message = cpu.gpr[4];
    const uint32_t flags = cpu.gpr[5];
    auto& state = message_queue(queue, cpu.pc);
    if (machine_ == nullptr) {
        fault("message queue requires Cafe scheduler", queue,
              abi::kOsMessageQueueSize, cpu.pc);
    }
    validate_range(queue, abi::kOsMessageQueueSize, cpu.pc,
                   MemoryAccess::write);
    const uint32_t messages = abi::read_u32(
        memory_, queue, abi::kMessageQueueMessagesOffset, cpu.pc);
    const uint32_t size =
        abi::read_u32(memory_, queue, abi::kMessageQueueSizeOffset, cpu.pc);
    const uint32_t first =
        abi::read_u32(memory_, queue, abi::kMessageQueueFirstOffset, cpu.pc);
    const uint32_t used =
        abi::read_u32(memory_, queue, abi::kMessageQueueUsedOffset, cpu.pc);
    if (used > size || (size != 0 && first >= size)) {
        fault("corrupt message queue ring", queue, abi::kOsMessageQueueSize,
              cpu.pc);
    }
    const uint64_t buffer_bytes = uint64_t{size} * abi::kOsMessageSize;
    if (buffer_bytes > std::numeric_limits<uint32_t>::max()) {
        fault("message buffer overflow", messages, abi::kOsMessageSize,
              cpu.pc);
    }
    memory_.validate_range(messages, static_cast<uint32_t>(buffer_bytes),
                           cpu.pc, MemoryAccess::read);
    memory_.validate_range(message, abi::kOsMessageSize, cpu.pc,
                           MemoryAccess::write);

    const uint32_t current = machine_->current_thread_address();
    const auto self = std::find_if(
        state.recv_waiters.begin(), state.recv_waiters.end(),
        [current](const MutexWaiter& waiter) {
            return waiter.thread == current;
        });
    if (used == 0) {
        if ((flags & abi::kMessageFlagBlocking) == 0) {
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        }
        if (self == state.recv_waiters.end()) {
            state.recv_waiters.push_back(
                {current, machine_->current_thread_priority(),
                 wait_sequence_++});
            mirror_thread_queue(queue, abi::kMessageQueueRecvQueueOffset,
                                state.recv_waiters, cpu.pc);
        }
        machine_->block_current(queue);
        return HleAction::reschedule;
    }
    if (self != state.recv_waiters.end()) {
        state.recv_waiters.erase(self);
        mirror_thread_queue(queue, abi::kMessageQueueRecvQueueOffset,
                            state.recv_waiters, cpu.pc);
    }

    const uint32_t source = messages + first * abi::kOsMessageSize;
    for (uint32_t offset = 0; offset < abi::kOsMessageSize; ++offset) {
        memory_.write8(message + offset,
                       memory_.read8(source + offset, cpu.pc), cpu.pc);
    }
    abi::write_u32(memory_, queue, abi::kMessageQueueFirstOffset,
                   (first + 1) % size, cpu.pc);
    abi::write_u32(memory_, queue, abi::kMessageQueueUsedOffset, used - 1,
                   cpu.pc);
    cpu.gpr[3] = 1;
    if (!state.send_waiters.empty()) {
        const auto woken = std::min_element(state.send_waiters.begin(),
                                            state.send_waiters.end(),
                                            waiter_before);
        const uint32_t thread = woken->thread;
        state.send_waiters.erase(woken);
        mirror_thread_queue(queue, abi::kMessageQueueSendQueueOffset,
                            state.send_waiters, cpu.pc);
        machine_->wake_thread(thread);
        cpu.pc = cpu.lr;
        return HleAction::reschedule;
    }
    return HleAction::return_to_lr;
}

void CafeCoreinit::register_handlers(CafeRuntime& runtime) {
    image_.imports.try_emplace(
        kErreulaStub, ImportTarget{"erreula", "unrendered_stub"});
    runtime.register_handler(
        "erreula", "unrendered_stub",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    image_.imports.try_emplace(
        kYaz0Decompress, ImportTarget{"runtime", "Yaz0Decompress"});
    runtime.register_handler(
        "runtime", "Yaz0Decompress",
        [](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t destination = cpu.gpr[3];
            const uint32_t source = cpu.gpr[4];
            if (memory.read32(source, cpu.pc) != 0x59617A30) {
                throw GuestFault("invalid Yaz0 header", source, 4, cpu.pc,
                                 MemoryAccess::read);
            }
            const uint32_t output_size = memory.read32(source + 4, cpu.pc);
            memory.validate_range(destination, output_size, cpu.pc,
                                  MemoryAccess::write);
            uint32_t input = source + 16;
            uint32_t output = destination;
            uint32_t code = 0;
            uint32_t bits = 0;
            while (output - destination < output_size) {
                if (bits == 0) {
                    code = memory.read8(input++, cpu.pc);
                    bits = 8;
                }
                if ((code & 0x80) != 0) {
                    memory.write8(output++, memory.read8(input++, cpu.pc),
                                  cpu.pc);
                } else {
                    const uint32_t first = memory.read8(input++, cpu.pc);
                    const uint32_t second = memory.read8(input++, cpu.pc);
                    uint32_t count = first >> 4;
                    count = count == 0
                                ? memory.read8(input++, cpu.pc) + 0x12
                                : count + 2;
                    const uint32_t distance =
                        ((first & 0xF) << 8) | second;
                    if (distance >= output - destination) {
                        throw GuestFault("invalid Yaz0 back-reference", input,
                                         1, cpu.pc, MemoryAccess::read);
                    }
                    uint32_t copy = output - distance - 1;
                    while (count-- != 0 &&
                           output - destination < output_size) {
                        memory.write8(output++, memory.read8(copy++, cpu.pc),
                                      cpu.pc);
                    }
                }
                code <<= 1;
                --bits;
            }
            cpu.gpr[3] = destination;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMAllocFromDefaultHeap",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = allocate_expanded(abi::kMem2Heap, cpu.gpr[3], 8,
                                           cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMAllocFromDefaultHeapEx",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = allocate_expanded(
                abi::kMem2Heap, cpu.gpr[3], static_cast<int32_t>(cpu.gpr[4]),
                cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMFreeToDefaultHeap",
        [this](CPUContext& cpu, GuestMemory&) {
            free_expanded(abi::kMem2Heap, cpu.gpr[3], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMCreateExpHeapEx",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = create_expanded_heap(cpu.gpr[3], cpu.gpr[4],
                                              cpu.gpr[5], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMDestroyExpHeap",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = destroy_expanded_heap(cpu.gpr[3], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMAllocFromExpHeapEx",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = allocate_expanded(
                cpu.gpr[3], cpu.gpr[4], static_cast<int32_t>(cpu.gpr[5]),
                cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMFreeToExpHeap",
        [this](CPUContext& cpu, GuestMemory&) {
            if (cpu.gpr[4] != 0) {
                free_expanded(cpu.gpr[3], cpu.gpr[4], cpu.pc);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMCreateFrmHeapEx",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] =
                create_frame_heap(cpu.gpr[3], cpu.gpr[4], cpu.gpr[5], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMDestroyFrmHeap",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = destroy_frame_heap(cpu.gpr[3], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMAllocFromFrmHeapEx",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = allocate_frame(cpu.gpr[3], cpu.gpr[4],
                                        static_cast<int32_t>(cpu.gpr[5]),
                                        cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMGetAllocatableSizeForExpHeapEx",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = expanded_allocatable(
                cpu.gpr[3], static_cast<int32_t>(cpu.gpr[4]), cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMGetAllocatableSizeForFrmHeapEx",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = frame_allocatable(
                cpu.gpr[3], static_cast<int32_t>(cpu.gpr[4]), cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMGetBaseHeapHandle",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = base_heap(cpu.gpr[3], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "MEMGetTotalFreeSizeForExpHeap",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = expanded_total_free(cpu.gpr[3], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSBlockMove",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t destination = cpu.gpr[3];
            const uint32_t source = cpu.gpr[4];
            const uint32_t count = cpu.gpr[5];
            memory.validate_range(source, count, cpu.pc, MemoryAccess::read);
            memory.validate_range(destination, count, cpu.pc,
                                  MemoryAccess::write);
            if (destination > source && destination - source < count) {
                for (uint32_t remaining = count; remaining != 0; --remaining) {
                    memory.write8(
                        destination + remaining - 1,
                        memory.read8(source + remaining - 1, cpu.pc), cpu.pc);
                }
            } else {
                for (uint32_t index = 0; index < count; ++index) {
                    memory.write8(destination + index,
                                  memory.read8(source + index, cpu.pc), cpu.pc);
                }
            }
            cpu.gpr[3] = destination;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSBlockSet",
        [](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t destination = cpu.gpr[3];
            const uint8_t value = static_cast<uint8_t>(cpu.gpr[4]);
            const uint32_t count = cpu.gpr[5];
            memory.validate_range(destination, count, cpu.pc,
                                  MemoryAccess::write);
            for (uint32_t index = 0; index < count; ++index) {
                memory.write8(destination + index, value, cpu.pc);
            }
            cpu.gpr[3] = destination;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSMemoryBarrier",
        [](CPUContext&, GuestMemory&) { return HleAction::return_to_lr; });
    runtime.register_handler(
        "coreinit", "DCZeroRange",
        [](CPUContext& cpu, GuestMemory& memory) {
            memory.validate_range(cpu.gpr[3], cpu.gpr[4], cpu.pc,
                                  MemoryAccess::write);
            for (uint32_t index = 0; index < cpu.gpr[4]; ++index) {
                memory.write8(cpu.gpr[3] + index, 0, cpu.pc);
            }
            return HleAction::return_to_lr;
        });
    const auto flush_range = [](CPUContext& cpu, GuestMemory& memory) {
        // The host runtime has unified CPU/GPU memory and no emulated data
        // cache, so Cafe cache range operations only preflight their source.
        memory.validate_range(cpu.gpr[3], cpu.gpr[4], cpu.pc,
                              MemoryAccess::read);
        return HleAction::return_to_lr;
    };
    runtime.register_handler("coreinit", "DCFlushRange", flush_range);
    runtime.register_handler("coreinit", "DCFlushRangeNoSync", flush_range);
    runtime.register_handler("coreinit", "DCStoreRange", flush_range);
    runtime.register_handler("coreinit", "DCStoreRangeNoSync", flush_range);
    runtime.register_handler("coreinit", "DCInvalidateRange", flush_range);
    runtime.register_handler(
        "coreinit", "OSIsAddressRangeDCValid",
        [](CPUContext& cpu, GuestMemory&) {
            constexpr uint32_t kInvalidBegin = 0xE8000000;
            constexpr uint32_t kInvalidEnd = 0xEC000000;
            const uint32_t begin = cpu.gpr[3];
            const uint32_t end = begin + cpu.gpr[4] - 1;
            cpu.gpr[3] = (begin < kInvalidBegin && end < kInvalidBegin) ||
                         (begin > kInvalidEnd && end > kInvalidEnd);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "UCOpen",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "UCClose",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "UCReadSysConfig",
        [](CPUContext& cpu, GuestMemory& memory) {
            constexpr uint32_t setting_size = 0x54;
            constexpr char language_key[] = "cafe.language";
            constexpr char parental_prefix[] = "p_acct1.";
            const uint32_t count = cpu.gpr[4];
            const uint32_t settings = cpu.gpr[5];
            if (cpu.gpr[3] != 1) {
                cpu.gpr[3] = 0xFFFFFFFFu;
                return HleAction::return_to_lr;
            }
            if (count > std::numeric_limits<uint32_t>::max() / setting_size) {
                throw GuestFault("invalid UCReadSysConfig count", settings,
                                 count, cpu.pc, MemoryAccess::read);
            }
            memory.validate_range(settings, count * setting_size, cpu.pc,
                                  MemoryAccess::read);
            memory.validate_range(settings, count * setting_size, cpu.pc,
                                  MemoryAccess::write);
            const auto matches = [&](uint32_t setting, const char* key,
                                     size_t size) {
                for (size_t ch = 0; ch < size; ++ch) {
                    if (memory.read8(setting + static_cast<uint32_t>(ch),
                                     cpu.pc) !=
                        static_cast<uint8_t>(key[ch])) {
                        return false;
                    }
                }
                return true;
            };
            for (uint32_t index = 0; index < count; ++index) {
                const uint32_t setting = settings + index * setting_size;
                const bool is_language =
                    matches(setting, language_key, sizeof(language_key));
                const bool is_parental = matches(
                    setting, parental_prefix, sizeof(parental_prefix) - 1);
                const uint32_t type = memory.read32(setting + 0x44, cpu.pc);
                const uint32_t size = memory.read32(setting + 0x4C, cpu.pc);
                const uint32_t output = memory.read32(setting + 0x50, cpu.pc);
                if ((!is_language || type != 3 || size < 4) &&
                    (!is_parental || type != 1 || size < 1)) {
                    throw GuestFault("unsupported UCReadSysConfig setting",
                                     setting, setting_size, cpu.pc,
                                     MemoryAccess::read);
                }
                memory.validate_range(output, is_language ? 4 : 1, cpu.pc,
                                      MemoryAccess::write);
            }
            for (uint32_t index = 0; index < count; ++index) {
                const uint32_t setting = settings + index * setting_size;
                const bool is_language =
                    matches(setting, language_key, sizeof(language_key));
                const uint32_t output =
                    memory.read32(setting + 0x50, cpu.pc);
                memory.write32(setting + 0x48, 0, cpu.pc);
                if (is_language) {
                    memory.write32(output, 1, cpu.pc);
                } else {
                    memory.write8(output, 0, cpu.pc);
                }
            }
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSGetSharedData",
        [this](CPUContext& cpu, GuestMemory& memory) {
            constexpr uint32_t standard_font = 2;
            if (cpu.gpr[3] != standard_font || shared_font_.empty() ||
                cpu.gpr[5] == 0 || cpu.gpr[6] == 0) {
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            }
            memory.validate(cpu.gpr[5], 4, cpu.pc, MemoryAccess::write);
            memory.validate(cpu.gpr[6], 4, cpu.pc, MemoryAccess::write);
            if (shared_font_address_ == 0) {
                shared_font_address_ = allocate_expanded(
                    abi::kMem2Heap, static_cast<uint32_t>(shared_font_.size()),
                    64, cpu.pc);
                for (size_t index = 0; index < shared_font_.size(); ++index) {
                    memory.write8(
                        shared_font_address_ + static_cast<uint32_t>(index),
                        shared_font_[index], cpu.pc);
                }
            }
            memory.write32(cpu.gpr[5], shared_font_address_, cpu.pc);
            memory.write32(cpu.gpr[6],
                           static_cast<uint32_t>(shared_font_.size()), cpu.pc);
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSReport",
        [](CPUContext&, GuestMemory&) { return HleAction::return_to_lr; });
    runtime.register_handler(
        "coreinit", "OSVReport",
        [](CPUContext&, GuestMemory&) { return HleAction::return_to_lr; });
    runtime.register_handler(
        "coreinit", "IMDisableDim",
        [this](CPUContext& cpu, GuestMemory&) {
            dim_enabled_ = false;
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSDynLoad_Acquire",
        [](CPUContext& cpu, GuestMemory& memory) {
            constexpr uint32_t invalid_name_pointer = 0xBAD1000F;
            constexpr uint32_t invalid_name = 0xBAD10010;
            constexpr uint32_t invalid_handle_pointer = 0xBAD10011;
            constexpr uint32_t module_not_found = 0xBAD10023;
            if (cpu.gpr[4] == 0) {
                cpu.gpr[3] = invalid_handle_pointer;
                return HleAction::return_to_lr;
            }
            if (cpu.gpr[3] == 0) {
                cpu.gpr[3] = invalid_name_pointer;
                return HleAction::return_to_lr;
            }
            memory.validate(cpu.gpr[4], 4, cpu.pc, MemoryAccess::write);
            if (memory.read8(cpu.gpr[3], cpu.pc) == 0) {
                cpu.gpr[3] = invalid_name;
                return HleAction::return_to_lr;
            }
            if (guest_string_equals(memory, cpu.gpr[3], "erreula.rpl",
                                    cpu.pc)) {
                memory.write32(cpu.gpr[4], kErreulaHandle, cpu.pc);
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            }
            if (guest_string_equals(memory, cpu.gpr[3], "swkbd.rpl",
                                    cpu.pc)) {
                memory.write32(cpu.gpr[4], kSwkbdHandle, cpu.pc);
                cpu.gpr[3] = 0;
                return HleAction::return_to_lr;
            }
            memory.write32(cpu.gpr[4], 0, cpu.pc);
            cpu.gpr[3] = module_not_found;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSDynLoad_FindExport",
        [](CPUContext& cpu, GuestMemory& memory) {
            constexpr uint32_t invalid_argument = 0xBAD1000F;
            constexpr uint32_t export_not_found = 0xBAD10028;
            if (cpu.gpr[6] == 0) {
                cpu.gpr[3] = invalid_argument;
                return HleAction::return_to_lr;
            }
            memory.validate(cpu.gpr[6], 4, cpu.pc, MemoryAccess::write);
            if ((cpu.gpr[3] != kErreulaHandle &&
                 cpu.gpr[3] != kSwkbdHandle) ||
                cpu.gpr[4] != 0 || cpu.gpr[5] == 0 ||
                memory.read8(cpu.gpr[5], cpu.pc) == 0) {
                memory.write32(cpu.gpr[6], 0, cpu.pc);
                cpu.gpr[3] = export_not_found;
                return HleAction::return_to_lr;
            }
            memory.write32(cpu.gpr[6], kErreulaStub, cpu.pc);
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSDynLoad_SetAllocator",
        [this](CPUContext& cpu, GuestMemory&) {
            constexpr uint32_t invalid_allocator = 0xBAD10017;
            if (cpu.gpr[3] == 0 || cpu.gpr[4] == 0) {
                cpu.gpr[3] = invalid_allocator;
                return HleAction::return_to_lr;
            }
            dynload_alloc_ = cpu.gpr[3];
            dynload_free_ = cpu.gpr[4];
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSCreateThread",
        [this](CPUContext& cpu, GuestMemory& memory) {
            if (machine_ == nullptr) {
                fault("thread creation requires Cafe scheduler", cpu.pc, 4,
                      cpu.pc);
            }

            const uint32_t thread = cpu.gpr[3];
            const uint32_t entry = cpu.gpr[4];
            const uint32_t argc = cpu.gpr[5];
            const uint32_t argv = cpu.gpr[6];
            const uint32_t stack = cpu.gpr[7];
            const uint32_t stack_size = cpu.gpr[8];
            const int32_t priority = static_cast<int32_t>(cpu.gpr[9]);
            uint8_t attributes = static_cast<uint8_t>(cpu.gpr[10]);

            if ((thread & 7) != 0) {
                fault("invalid OSThread alignment", thread,
                      abi::kOsThreadSize, cpu.pc, MemoryAccess::write);
            }
            if (entry == 0 || (entry & 3) != 0) {
                fault("invalid thread entry", entry, 4, cpu.pc,
                      MemoryAccess::execute);
            }
            if ((stack & 3) != 0 || stack_size < 8 ||
                (stack_size & 3) != 0 || stack < stack_size) {
                fault("invalid thread stack", stack, stack_size, cpu.pc,
                      MemoryAccess::write);
            }
            if (priority < 0 || priority > 31) {
                fault("thread priority out of range", cpu.gpr[9], 4, cpu.pc);
            }
            if (machine_->has_thread(thread)) {
                fault("duplicate OSThread", thread, abi::kOsThreadSize,
                      cpu.pc, MemoryAccess::write);
            }

            const uint32_t stack_end = stack - stack_size;
            const uint32_t aligned_stack = stack & ~uint32_t{7};
            validate_range(thread, abi::kOsThreadSize, cpu.pc,
                           MemoryAccess::write);
            validate_range(stack_end, stack_size, cpu.pc,
                           MemoryAccess::write);
            memory.validate(entry, 4, cpu.pc, MemoryAccess::execute);

            const auto exit_import = std::find_if(
                image_.imports.begin(), image_.imports.end(),
                [](const auto& item) {
                    return item.second.module == "coreinit" &&
                           item.second.symbol == "OSExitThread";
                });
            if (exit_import == image_.imports.end()) {
                fault("missing OSExitThread import", entry, 4, cpu.pc,
                      MemoryAccess::execute);
            }

            if ((attributes & 7) == 0) {
                attributes |= abi::read_u8(
                    memory_, machine_->current_thread_address(),
                    abi::kOsThreadAttrOffset, cpu.pc) & 7;
            }
            const uint8_t affinity = attributes & 7;
            if (affinity == 0) {
                fault("thread has no affinity", thread, abi::kOsThreadSize,
                      cpu.pc);
            }

            CPUContext created;
            created.pc = entry;
            created.lr = exit_import->first;
            created.gpr[1] = aligned_stack - 8;
            created.gpr[2] = cpu.gpr[2];
            created.gpr[3] = argc;
            created.gpr[4] = argv;
            created.gpr[13] = cpu.gpr[13];
            created.fpscr = 4 | (cpu.fpscr & 0xF8);

            for (uint32_t offset = 0; offset < abi::kOsThreadSize; ++offset) {
                memory.write8(thread + offset, 0, cpu.pc);
            }
            abi::write_u64(memory_, thread, abi::kOsContextTagOffset,
                           abi::kOsContextTag, cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGprOffset + 1 * 4,
                           created.gpr[1], cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGprOffset + 2 * 4,
                           created.gpr[2], cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGprOffset + 3 * 4, argc, cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGprOffset + 4 * 4, argv, cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGprOffset + 13 * 4,
                           created.gpr[13], cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsContextLrOffset,
                           created.lr, cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsContextSrr0Offset,
                           created.pc, cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsContextFpscrOffset,
                           created.fpscr, cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGqrOffset + 2 * 4, 0x40004,
                           cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGqrOffset + 3 * 4, 0x50005,
                           cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGqrOffset + 4 * 4, 0x60006,
                           cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsContextGqrOffset + 5 * 4, 0x70007,
                           cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsContextPirOffset, 1,
                           cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsContextAttrOffset,
                           affinity, cpu.pc);

            abi::write_u32(memory_, thread, abi::kOsThreadTagOffset,
                           abi::kOsThreadTag, cpu.pc);
            abi::write_u8(memory_, thread, abi::kOsThreadStateOffset,
                          abi::kThreadReady, cpu.pc);
            abi::write_u8(memory_, thread, abi::kOsThreadAttrOffset,
                          attributes, cpu.pc);
            abi::write_u16(memory_, thread, abi::kOsThreadIdOffset,
                           next_thread_id_, cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsThreadSuspendOffset, 1,
                           cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsThreadPriorityOffset,
                           static_cast<uint32_t>(priority + 64), cpu.pc);
            abi::write_u32(memory_, thread,
                           abi::kOsThreadBasePriorityOffset,
                           static_cast<uint32_t>(priority + 64), cpu.pc);
            abi::write_u32(memory_, thread, 0x334, 0xFFFFFFFF, cpu.pc);
            abi::write_u32(memory_, thread, 0x370, thread, cpu.pc);
            abi::write_u32(memory_, thread, 0x384, thread, cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsThreadStackStartOffset,
                           stack, cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsThreadStackEndOffset,
                           stack_end, cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsThreadEntryOffset, entry,
                           cpu.pc);
            abi::write_u32(memory_, thread, abi::kOsThreadTypeOffset, 2,
                           cpu.pc);
            abi::write_u32(memory_, thread, 0x5D4, 1, cpu.pc);
            abi::write_u32(memory_, thread, 0x5E0, 0xFFFFFFFF, cpu.pc);
            abi::write_u32(memory_, thread, 0x5EC, thread, cpu.pc);
            abi::write_u64(memory_, thread, 0x620,
                           0x7FFFFFFFFFFFFFFFULL, cpu.pc);

            memory.write32(aligned_stack - 4, 0, cpu.pc);
            memory.write32(aligned_stack - 8, 0, cpu.pc);
            memory.write32(stack_end, 0xDEADBABE, cpu.pc);
            machine_->add_suspended_thread(thread, std::move(created),
                                           priority, affinity);
            ++next_thread_id_;
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSExitThread",
        [this](CPUContext& cpu, GuestMemory&) {
            if (machine_ == nullptr) {
                fault("thread exit requires Cafe scheduler", cpu.pc, 4,
                      cpu.pc);
            }
            if (!machine_->return_from_callback()) {
                machine_->exit_current(static_cast<int32_t>(cpu.gpr[3]));
            }
            return HleAction::reschedule;
        });
    runtime.register_handler(
        "coreinit", "OSCreateAlarm",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t alarm = cpu.gpr[3];
            validate_range(alarm, abi::kOsAlarmSize, cpu.pc,
                           MemoryAccess::write);
            for (uint32_t offset = 0; offset < abi::kOsAlarmSize;
                 offset += 4) {
                abi::write_u32(memory_, alarm, offset, 0, cpu.pc);
            }
            abi::write_u32(memory_, alarm, abi::kAlarmTagOffset,
                           abi::kOsAlarmTag, cpu.pc);
            abi::write_u32(
                memory_, alarm,
                abi::kAlarmThreadQueueOffset +
                    abi::kThreadQueueParentOffset,
                alarm, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSSetAlarmUserData",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t alarm = cpu.gpr[3];
            validate_range(alarm, abi::kOsAlarmSize, cpu.pc,
                           MemoryAccess::read);
            validate_range(alarm + abi::kAlarmUserDataOffset, 4, cpu.pc,
                           MemoryAccess::write);
            if (abi::read_u32(memory_, alarm, abi::kAlarmTagOffset,
                              cpu.pc) != abi::kOsAlarmTag) {
                fault("invalid alarm", alarm, abi::kOsAlarmSize, cpu.pc,
                      MemoryAccess::read);
            }
            abi::write_u32(memory_, alarm, abi::kAlarmUserDataOffset,
                           cpu.gpr[4], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSSetPeriodicAlarm",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t alarm = cpu.gpr[3];
            validate_range(alarm, abi::kOsAlarmSize, cpu.pc,
                           MemoryAccess::read);
            validate_range(alarm, abi::kOsAlarmSize, cpu.pc,
                           MemoryAccess::write);
            if (abi::read_u32(memory_, alarm, abi::kAlarmTagOffset,
                              cpu.pc) != abi::kOsAlarmTag) {
                fault("invalid alarm", alarm, abi::kOsAlarmSize, cpu.pc,
                      MemoryAccess::read);
            }
            uint64_t start = static_cast<uint64_t>(cpu.gpr[5]) << 32 |
                             cpu.gpr[6];
            const uint64_t interval =
                static_cast<uint64_t>(cpu.gpr[7]) << 32 | cpu.gpr[8];
            if (start == 0) {
                start = (machine_ == nullptr ? ticks_
                                             : machine_->current_time_ticks()) +
                        interval;
            }
            abi::write_u32(memory_, alarm, abi::kAlarmCallbackOffset,
                           cpu.gpr[9], cpu.pc);
            abi::write_u64(memory_, alarm, abi::kAlarmNextFireOffset, start,
                           cpu.pc);
            abi::write_u64(memory_, alarm, abi::kAlarmPeriodOffset, interval,
                           cpu.pc);
            abi::write_u32(memory_, alarm, abi::kAlarmStateOffset, 1, cpu.pc);
            abi::write_u32(memory_, alarm, abi::kAlarmContextOffset, 0,
                           cpu.pc);
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSYieldThread",
        [this](CPUContext& cpu, GuestMemory&) {
            if (machine_ == nullptr) {
                fault("thread yield requires Cafe scheduler", cpu.pc, 4,
                      cpu.pc);
            }
            cpu.pc = cpu.lr;
            return HleAction::reschedule;
        });
    runtime.register_handler(
        "coreinit", "OSGetCoreId",
        [this](CPUContext& cpu, GuestMemory&) {
            if (machine_ == nullptr) {
                fault("core id requires Cafe scheduler", cpu.pc, 4, cpu.pc);
            }
            cpu.gpr[3] = machine_->current_core_id();
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSDisableInterrupts",
        [this](CPUContext& cpu, GuestMemory&) {
            bool& enabled = interrupt_state(cpu.pc);
            const bool previous = enabled;
            enabled = false;
            cpu.gpr[3] = previous;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSEnableInterrupts",
        [this](CPUContext& cpu, GuestMemory&) {
            bool& enabled = interrupt_state(cpu.pc);
            const bool previous = enabled;
            enabled = true;
            cpu.gpr[3] = previous;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSRestoreInterrupts",
        [this](CPUContext& cpu, GuestMemory&) {
            bool& enabled = interrupt_state(cpu.pc);
            const bool previous = enabled;
            enabled = cpu.gpr[3] != 0;
            cpu.gpr[3] = previous;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSIsInterruptEnabled",
        [this](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = interrupt_state(cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSGetCurrentThread",
        [this](CPUContext& cpu, GuestMemory&) {
            if (machine_ == nullptr) {
                fault("current thread requires Cafe scheduler", cpu.pc, 4,
                      cpu.pc);
            }
            cpu.gpr[3] = machine_->current_thread_address();
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSGetSystemInfo",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = abi::kOsSystemInfo;
            return HleAction::return_to_lr;
        });
    const char* const clock_symbols[] = {"OSGetSystemTime", "OSGetTime"};
    for (const char* symbol : clock_symbols) {
        runtime.register_handler(
            "coreinit", symbol,
            [this](CPUContext& cpu, GuestMemory&) {
                const uint64_t time =
                    machine_ == nullptr ? ticks_
                                        : machine_->current_time_ticks();
                cpu.gpr[3] = static_cast<uint32_t>(time >> 32);
                cpu.gpr[4] = static_cast<uint32_t>(time);
                return HleAction::return_to_lr;
            });
    }
    runtime.register_handler(
        "coreinit", "OSGetTick",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint64_t time =
                machine_ == nullptr ? ticks_ : machine_->current_time_ticks();
            cpu.gpr[3] = static_cast<uint32_t>(time);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSGetThreadSpecific",
        [this](CPUContext& cpu, GuestMemory&) {
            if (machine_ == nullptr || cpu.gpr[3] >= 16) {
                fault("invalid thread-specific id", cpu.gpr[3], 4, cpu.pc);
            }
            cpu.gpr[3] = abi::read_u32(
                memory_, machine_->current_thread_address(),
                abi::kOsThreadSpecificOffset + cpu.gpr[3] * 4, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSGetThreadPriority",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t thread = cpu.gpr[3];
            validate_range(thread, abi::kOsThreadSize, cpu.pc,
                           MemoryAccess::read);
            const uint32_t base_priority = abi::read_u32(
                memory_, thread, abi::kOsThreadBasePriorityOffset, cpu.pc);
            switch (abi::read_u32(memory_, thread, abi::kOsThreadTypeOffset,
                                  cpu.pc)) {
            case 0:
                cpu.gpr[3] = base_priority;
                break;
            case 1:
                cpu.gpr[3] = base_priority - 32;
                break;
            case 2:
                cpu.gpr[3] = base_priority - 64;
                break;
            default:
                fault("invalid thread type",
                      thread + abi::kOsThreadTypeOffset, 4, cpu.pc);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSResumeThread",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t thread = cpu.gpr[3];
            const uint32_t counter_address =
                thread + abi::kOsThreadSuspendOffset;
            memory_.validate(counter_address, 4, cpu.pc,
                             MemoryAccess::read);
            memory_.validate(counter_address, 4, cpu.pc,
                             MemoryAccess::write);
            if (machine_ == nullptr || !machine_->has_thread(thread)) {
                fault("unknown Cafe thread", thread, abi::kOsThreadSize,
                      cpu.pc);
            }

            const int32_t old = static_cast<int32_t>(
                abi::read_u32(memory_, thread, abi::kOsThreadSuspendOffset,
                              cpu.pc));
            const int32_t counter = old > 0 ? old - 1 : 0;
            abi::write_u32(memory_, thread, abi::kOsThreadSuspendOffset,
                           static_cast<uint32_t>(counter), cpu.pc);
            cpu.gpr[3] = static_cast<uint32_t>(old);
            if (old == 1) {
                machine_->resume_thread(thread);
                cpu.pc = cpu.lr;
                return HleAction::reschedule;
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSSetThreadAffinity",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t thread = cpu.gpr[3];
            const uint8_t affinity = static_cast<uint8_t>(cpu.gpr[4]);
            memory_.validate(thread + abi::kOsThreadAttrOffset, 1, cpu.pc,
                             MemoryAccess::write);
            const uint8_t attributes =
                abi::read_u8(memory_, thread, abi::kOsThreadAttrOffset,
                             cpu.pc);
            if (machine_ != nullptr && machine_->has_thread(thread)) {
                machine_->set_thread_affinity(thread, affinity);
            }
            abi::write_u8(memory_, thread, abi::kOsThreadAttrOffset,
                          static_cast<uint8_t>((attributes & ~uint8_t{7}) |
                                               affinity),
                          cpu.pc);
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSSetThreadName",
        [this](CPUContext& cpu, GuestMemory&) {
            abi::write_u32(memory_, cpu.gpr[3], abi::kOsThreadNameOffset,
                           cpu.gpr[4], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSSetThreadSpecific",
        [this](CPUContext& cpu, GuestMemory&) {
            if (machine_ == nullptr || cpu.gpr[3] >= 16) {
                fault("invalid thread-specific id", cpu.gpr[3], 4, cpu.pc);
            }
            abi::write_u32(
                memory_, machine_->current_thread_address(),
                abi::kOsThreadSpecificOffset + cpu.gpr[3] * 4, cpu.gpr[4],
                cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSInitEvent",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t event = cpu.gpr[3];
            validate_range(event, abi::kOsEventSize, cpu.pc,
                           MemoryAccess::write);
            abi::write_u32(memory_, event, 0, abi::kOsEventTag, cpu.pc);
            abi::write_u32(memory_, event, abi::kEventNameOffset, 0, cpu.pc);
            abi::write_u32(memory_, event, abi::kEventValueOffset,
                           cpu.gpr[4] != 0, cpu.pc);
            abi::write_u32(memory_, event, abi::kEventQueueOffset, 0, cpu.pc);
            abi::write_u32(memory_, event, abi::kEventQueueOffset + 4, 0,
                           cpu.pc);
            abi::write_u32(memory_, event, abi::kEventQueueOffset + 8, event,
                           cpu.pc);
            abi::write_u32(memory_, event, abi::kEventModeOffset, cpu.gpr[5],
                           cpu.pc);
            events_.insert_or_assign(event, EventState{});
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSResetEvent",
        [this](CPUContext& cpu, GuestMemory&) {
            static_cast<void>(event(cpu.gpr[3], cpu.pc));
            abi::write_u32(memory_, cpu.gpr[3], abi::kEventValueOffset, 0,
                           cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSSignalEvent",
        [this](CPUContext& cpu, GuestMemory&) {
            return signal_event(cpu, false);
        });
    runtime.register_handler(
        "coreinit", "OSSignalEventAll",
        [this](CPUContext& cpu, GuestMemory&) {
            return signal_event(cpu, true);
        });
    runtime.register_handler(
        "coreinit", "OSWaitEvent",
        [this](CPUContext& cpu, GuestMemory&) {
            return wait_event(cpu);
        });
    runtime.register_handler(
        "coreinit", "OSInitMutex",
        [this](CPUContext& cpu, GuestMemory&) {
            init_mutex(cpu.gpr[3], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSInitThreadQueue",
        [this](CPUContext& cpu, GuestMemory&) {
            validate_range(
                cpu.gpr[3], abi::kThreadQueueParentOffset + 4, cpu.pc,
                MemoryAccess::write);
            abi::write_u32(memory_, cpu.gpr[3],
                           abi::kThreadQueueHeadOffset, 0, cpu.pc);
            abi::write_u32(memory_, cpu.gpr[3],
                           abi::kThreadQueueTailOffset, 0, cpu.pc);
            abi::write_u32(memory_, cpu.gpr[3],
                           abi::kThreadQueueParentOffset, 0, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSInitRendezvous",
        [this](CPUContext& cpu, GuestMemory&) {
            validate_range(cpu.gpr[3], 0x0C, cpu.pc, MemoryAccess::write);
            for (uint32_t offset = 0; offset < 0x0C; offset += 4) {
                abi::write_u32(memory_, cpu.gpr[3], offset, 0, cpu.pc);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSWaitRendezvous",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t rendezvous = cpu.gpr[3];
            const uint32_t core_mask = cpu.gpr[4];
            if (machine_ == nullptr) {
                fault("rendezvous requires Cafe scheduler", rendezvous, 0x10,
                      cpu.pc);
            }
            validate_range(rendezvous, 0x0C, cpu.pc, MemoryAccess::read);
            validate_range(rendezvous, 0x0C, cpu.pc, MemoryAccess::write);
            abi::write_u32(memory_, rendezvous,
                           machine_->current_core_id() * 4, 1, cpu.pc);
            bool complete = true;
            for (uint32_t core = 0; core < 3; ++core) {
                if ((core_mask & (1u << core)) != 0 &&
                    abi::read_u32(memory_, rendezvous, core * 4, cpu.pc) == 0) {
                    complete = false;
                }
            }
            if (!complete) {
                machine_->block_current(rendezvous);
                return HleAction::reschedule;
            }
            machine_->wake_waiters(rendezvous);
            cpu.gpr[3] = 1;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSInitMessageQueue",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t queue = cpu.gpr[3];
            validate_range(queue, abi::kOsMessageQueueSize, cpu.pc,
                           MemoryAccess::write);
            abi::write_u32(memory_, queue, abi::kMessageQueueTagOffset,
                           abi::kOsMessageQueueTag, cpu.pc);
            abi::write_u32(memory_, queue, abi::kMessageQueueNameOffset, 0,
                           cpu.pc);
            for (const uint32_t offset :
                 {abi::kMessageQueueSendQueueOffset,
                  abi::kMessageQueueRecvQueueOffset}) {
                abi::write_u32(memory_, queue,
                               offset + abi::kThreadQueueHeadOffset, 0,
                               cpu.pc);
                abi::write_u32(memory_, queue,
                               offset + abi::kThreadQueueTailOffset, 0,
                               cpu.pc);
                abi::write_u32(memory_, queue,
                               offset + abi::kThreadQueueParentOffset, queue,
                               cpu.pc);
            }
            abi::write_u32(memory_, queue, abi::kMessageQueueMessagesOffset,
                           cpu.gpr[4], cpu.pc);
            abi::write_u32(memory_, queue, abi::kMessageQueueSizeOffset,
                           cpu.gpr[5], cpu.pc);
            abi::write_u32(memory_, queue, abi::kMessageQueueFirstOffset, 0,
                           cpu.pc);
            abi::write_u32(memory_, queue, abi::kMessageQueueUsedOffset, 0,
                           cpu.pc);
            message_queues_.insert_or_assign(queue, MessageQueueState{});
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSSendMessage",
        [this](CPUContext& cpu, GuestMemory&) { return send_message(cpu); });
    runtime.register_handler(
        "coreinit", "OSReceiveMessage",
        [this](CPUContext& cpu, GuestMemory&) {
            return receive_message(cpu);
        });
    runtime.register_handler(
        "coreinit", "OSSleepTicks",
        [this](CPUContext& cpu, GuestMemory&) {
            if (machine_ == nullptr) {
                fault("sleep requires Cafe scheduler", cpu.pc, 4, cpu.pc);
            }
            const int64_t ticks = static_cast<int64_t>(
                (uint64_t{cpu.gpr[3]} << 32) | cpu.gpr[4]);
            if (ticks <= 0) {
                return HleAction::return_to_lr;
            }
            machine_->sleep_current(static_cast<uint64_t>(ticks));
            cpu.pc = cpu.lr;
            return HleAction::reschedule;
        });
    runtime.register_handler(
        "coreinit", "OSEnableHomeButtonMenu",
        [this](CPUContext& cpu, GuestMemory&) {
            home_button_menu_enabled_ = cpu.gpr[3] != 0;
            cpu.gpr[3] = 1; // TRUE
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "IMEnableDim", [this](CPUContext& cpu, GuestMemory&) {
            dim_enabled_ = true;
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "IMIsDimEnabled",
        [this](CPUContext& cpu, GuestMemory& memory) {
            memory.write32(cpu.gpr[3], dim_enabled_, cpu.pc);
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSIsDebuggerInitialized",
        [](CPUContext& cpu, GuestMemory&) {
            cpu.gpr[3] = 0;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSLockMutex",
        [this](CPUContext& cpu, GuestMemory&) {
            return lock_mutex(cpu.gpr[3], cpu);
        });
    runtime.register_handler(
        "coreinit", "OSTryLockMutex",
        [this](CPUContext& cpu, GuestMemory&) {
            return try_lock_mutex(cpu.gpr[3], cpu);
        });
    runtime.register_handler(
        "coreinit", "OSSetExceptionCallback",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t type = cpu.gpr[3];
            if (type >= exception_callbacks_.size()) {
                fault("invalid exception type", type, 4, cpu.pc);
            }
            const uint32_t previous = exception_callbacks_[type];
            exception_callbacks_[type] = cpu.gpr[4];
            cpu.gpr[3] = previous;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "OSUnlockMutex",
        [this](CPUContext& cpu, GuestMemory&) {
            return unlock_mutex(cpu.gpr[3], cpu);
        });
    runtime.register_handler(
        "coreinit", "__gh_set_errno",
        [](CPUContext& cpu, GuestMemory& memory) {
            memory.write32(abi::kGhsErrno, cpu.gpr[3], cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "__ghs_flock_destroy",
        [this](CPUContext& cpu, GuestMemory&) {
            if (cpu.gpr[3] >= abi::kGhsIobCount) {
                fault("invalid GHS file lock index", cpu.gpr[3], 4, cpu.pc);
            }
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "__ghs_flock_file",
        [this](CPUContext& cpu, GuestMemory&) {
            if (cpu.gpr[3] >= abi::kGhsIobCount) {
                fault("invalid GHS file lock index", cpu.gpr[3], 4, cpu.pc);
            }
            return lock_mutex(abi::kGhsMutex, cpu);
        });
    runtime.register_handler(
        "coreinit", "__ghs_flock_ptr",
        [this](CPUContext& cpu, GuestMemory&) {
            const uint32_t pointer = cpu.gpr[3];
            uint32_t base = abi::kGhsIob;
            const auto in_iob = [pointer](uint32_t candidate) {
                return pointer >= candidate &&
                       pointer <
                           candidate +
                               abi::kGhsIobEntrySize * abi::kGhsIobCount &&
                       (pointer - candidate) % abi::kGhsIobEntrySize == 0;
            };
            if (!in_iob(base)) {
                const auto alias = std::find_if(
                    image_.imports.begin(), image_.imports.end(),
                    [&in_iob](const auto& entry) {
                        return entry.second.module == "coreinit" &&
                               entry.second.symbol == "_iob" &&
                               in_iob(entry.first);
                    });
                if (alias == image_.imports.end()) {
                    fault("invalid GHS file pointer", pointer,
                          abi::kGhsIobEntrySize, cpu.pc);
                }
                base = alias->first;
            }
            const uint32_t index =
                (pointer - base) / abi::kGhsIobEntrySize;
            cpu.gpr[3] = abi::kGhsFlocks + index * sizeof(uint32_t);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "__ghs_funlock_file",
        [this](CPUContext& cpu, GuestMemory&) {
            if (cpu.gpr[3] >= abi::kGhsIobCount) {
                fault("invalid GHS file lock index", cpu.gpr[3], 4, cpu.pc);
            }
            return unlock_mutex(abi::kGhsMutex, cpu);
        });
    runtime.register_handler(
        "coreinit", "__ghsLock",
        [this](CPUContext& cpu, GuestMemory&) {
            return lock_mutex(abi::kGhsMutex, cpu);
        });
    runtime.register_handler(
        "coreinit", "__ghsUnlock",
        [this](CPUContext& cpu, GuestMemory&) {
            return unlock_mutex(abi::kGhsMutex, cpu);
        });
    runtime.register_handler(
        "coreinit", "__ghs_mtx_dst",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t pointer = cpu.gpr[3];
            const uint32_t address = pointer_to_mutex(cpu, memory);
            auto& state = mutex(address, cpu.pc);
            if (state.owner != 0 || !state.waiters.empty()) {
                fault("destroying locked GHS mutex", address,
                      abi::kOsMutexSize, cpu.pc);
            }
            mutexes_.erase(address);
            free_expanded(abi::kMem2Heap, address, cpu.pc);
            validate_range(pointer, 4, cpu.pc, MemoryAccess::write);
            memory.write32(pointer, 0, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "__ghs_mtx_init",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t pointer = cpu.gpr[3];
            if ((pointer & 3) != 0) {
                fault("invalid GHS mutex pointer alignment", pointer, 4,
                      cpu.pc);
            }
            validate_range(pointer, 4, cpu.pc, MemoryAccess::write);
            const uint32_t address = allocate_expanded(
                abi::kMem2Heap, abi::kOsMutexSize, 8, cpu.pc);
            if (address == 0) {
                fault("GHS mutex heap exhausted", abi::kMem2Heap, 4,
                      cpu.pc);
            }
            init_mutex(address, cpu.pc);
            memory.write32(pointer, address, cpu.pc);
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "__ghs_mtx_lock",
        [this](CPUContext& cpu, GuestMemory& memory) {
            return lock_mutex(pointer_to_mutex(cpu, memory), cpu);
        });
    runtime.register_handler(
        "coreinit", "__ghs_mtx_unlock",
        [this](CPUContext& cpu, GuestMemory& memory) {
            return unlock_mutex(pointer_to_mutex(cpu, memory), cpu);
        });
    runtime.register_handler(
        "coreinit", "exit", [](CPUContext&, GuestMemory&) {
            return HleAction::exit;
        });
    runtime.register_handler(
        "coreinit", "memmove",
        [](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t destination = cpu.gpr[3];
            const uint32_t source = cpu.gpr[4];
            const uint32_t count = cpu.gpr[5];
            memory.validate_range(source, count, cpu.pc, MemoryAccess::read);
            memory.validate_range(destination, count, cpu.pc,
                                  MemoryAccess::write);
            std::array<uint8_t, 4096> buffer{};
            if (destination > source && destination - source < count) {
                uint32_t remaining = count;
                while (remaining != 0) {
                    const uint32_t amount =
                        std::min<uint32_t>(buffer.size(), remaining);
                    remaining -= amount;
                    const std::span chunk(buffer.data(), amount);
                    memory.read_bytes(source + remaining, chunk, cpu.pc);
                    memory.write_bytes(destination + remaining, chunk, cpu.pc);
                }
            } else {
                for (uint32_t copied = 0; copied < count;) {
                    const uint32_t amount =
                        std::min<uint32_t>(buffer.size(), count - copied);
                    const std::span chunk(buffer.data(), amount);
                    memory.read_bytes(source + copied, chunk, cpu.pc);
                    memory.write_bytes(destination + copied, chunk, cpu.pc);
                    copied += amount;
                }
            }
            cpu.gpr[3] = destination;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "memcpy",
        [](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t destination = cpu.gpr[3];
            const uint32_t source = cpu.gpr[4];
            const uint32_t count = cpu.gpr[5];
            memory.validate_range(source, count, cpu.pc, MemoryAccess::read);
            memory.validate_range(destination, count, cpu.pc,
                                  MemoryAccess::write);
            std::array<uint8_t, 4096> buffer{};
            for (uint32_t copied = 0; copied < count;) {
                const uint32_t amount =
                    std::min<uint32_t>(buffer.size(), count - copied);
                const std::span chunk(buffer.data(), amount);
                memory.read_bytes(source + copied, chunk, cpu.pc);
                memory.write_bytes(destination + copied, chunk, cpu.pc);
                copied += amount;
            }
            cpu.gpr[3] = destination;
            return HleAction::return_to_lr;
        });
    runtime.register_handler(
        "coreinit", "memset",
        [this](CPUContext& cpu, GuestMemory& memory) {
            const uint32_t destination = cpu.gpr[3];
            const uint8_t value = static_cast<uint8_t>(cpu.gpr[4]);
            const uint32_t count = cpu.gpr[5];
            memory.validate_range(destination, count, cpu.pc,
                                  MemoryAccess::write);
            const std::array pattern{value};
            memory.fill(destination, count, pattern, cpu.pc);
            cpu.gpr[3] = destination;
            return HleAction::return_to_lr;
        });
}
} // namespace nwii::runtime
