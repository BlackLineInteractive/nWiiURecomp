#include "nwiiu/recomp/runner_cli.h"

#include "nwiiu/analyzer/rpx.h"
#include "nwiiu/analyzer/target.h"
#include "runtime/machine.h"

#include <exception>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    try {
        std::vector<std::string_view> arguments;
        arguments.reserve(argc > 1 ? static_cast<size_t>(argc - 1) : 0);
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }

        const auto options = nwiiu::recomp::parse_runner_options(arguments);
        auto rpx = nwiiu::analyzer::load_rpx(
            options.input, nwiiu::analyzer::resolve_target());
        auto image = nwii::runtime::make_execution_image(rpx);
        nwii::runtime::Machine machine(image, options.title_root,
                                       options.save_root, options.shared_font);
        machine.executor().set_trace_enabled(options.trace);
        const auto stop = machine.run(options.max_instructions,
                                      nwii::runtime::kSchedulerQuantum);
        std::cout << nwiiu::recomp::format_stop(stop);
        if (options.trace) {
            std::cerr << nwiiu::recomp::format_trace(stop);
        }
        return stop.category == nwii::runtime::StopCategory::guest_exit ? 0
                                                                        : 3;
    } catch (const std::exception& error) {
        std::cerr << "INPUT ERROR: " << error.what() << '\n';
        return 2;
    }
}
