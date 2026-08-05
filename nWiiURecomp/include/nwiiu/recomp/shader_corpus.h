#pragma once

#include "nwiiu/recomp/shader_types.h"

#include <filesystem>

namespace nwiiu::recomp {

// Intermediate format between `shader-extract` and the later `shader-build`
// stage. Both return false instead of throwing: a damaged corpus must be a
// reported failure, not a crash (section 4.5).
bool store_corpus(const std::filesystem::path& path,
                  const std::vector<RawShader>& shaders);
bool load_corpus(const std::filesystem::path& path,
                 std::vector<RawShader>& out);

}  // namespace nwiiu::recomp
