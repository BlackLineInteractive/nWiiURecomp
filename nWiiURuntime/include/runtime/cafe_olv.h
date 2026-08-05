#pragma once

#include <cstdint>

namespace nwii::runtime {
class CafeRuntime;
class ExecutionImage;
class GuestMemory;

class CafeOlv {
public:
    static constexpr uint32_t kDownloadedPostDataVtable = 0x20000F00;

    explicit CafeOlv(ExecutionImage& image);
    CafeOlv(const CafeOlv&) = delete;
    CafeOlv& operator=(const CafeOlv&) = delete;
    CafeOlv(CafeOlv&&) = delete;
    CafeOlv& operator=(CafeOlv&&) = delete;

    void register_handlers(CafeRuntime& runtime);

private:
    GuestMemory& memory_;
    bool initialized_{};
};
} // namespace nwii::runtime
