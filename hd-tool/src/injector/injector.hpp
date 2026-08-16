#pragma once

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>

namespace hdtool::injector {

    struct inject_options {
        pid_t pid{};
        std::string library_path;
        std::string bridge_module{"libnativebridge.so"};
        std::optional<std::uintptr_t> bridge_rva;
        std::optional<std::uintptr_t> bridge_address;
    };

    struct inject_result {
        std::uintptr_t bridge_base{};
        std::uintptr_t loader_address{};
        std::uintptr_t handle{};
    };

    [[nodiscard]] bool inject_guest_library(const inject_options &options,
                                            inject_result &result,
                                            std::string &error);

} // namespace hdtool::injector
