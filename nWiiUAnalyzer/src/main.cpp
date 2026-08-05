#include "nwiiu/analyzer/analysis.h"
#include "nwiiu/analyzer/manifest.h"
#include "nwiiu/analyzer/rpx.h"
#include "nwiiu/analyzer/target.h"
#include <exception>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::cerr << "usage: nwiiu-analyze <cking.rpx> <manifest.json>\n";
        return 2;
    }
    try {
        const auto image = nwiiu::analyzer::load_rpx(
            argv[1], nwiiu::analyzer::resolve_target());
        const auto analysis = nwiiu::analyzer::analyze(image);
        nwiiu::analyzer::write_manifest_file(argv[2], image, analysis);
        std::cout << "RPX: " << image.sha256 << '\n'
                  << "Entry: 0x028EA9E0\n"
                  << "Sections: " << image.sections.size() << '\n'
                  << "Imports: " << image.imports.size() << '\n'
                  << "Relocations: " << image.relocations.size() << '\n'
                  << "Functions: " << analysis.functions.size() << '\n'
                  << "Unresolved: " << analysis.unresolved.size() << '\n';
        return analysis.unresolved.empty() ? 0 : 3;
    } catch (const std::exception& error) {
        std::cerr << "nwiiu-analyze: " << error.what() << '\n';
        return 2;
    }
}
