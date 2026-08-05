#pragma once
#include "nwiiu/analyzer/analysis.h"
#include <filesystem>
#include <iosfwd>

namespace nwiiu::analyzer {
void write_manifest(std::ostream& out, const RpxImage& image,
                    const Analysis& analysis);
void write_manifest_file(const std::filesystem::path& output,
                         const RpxImage& image, const Analysis& analysis);
}
