#pragma once

#include <sys/types.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace hdtool::memory {

    struct mapping {
        std::uintptr_t start{};
        std::uintptr_t end{};
        std::uintptr_t file_offset{};
        std::string permissions;
        std::string path;

        [[nodiscard]] bool executable() const;
        [[nodiscard]] std::size_t size() const;
    };

    struct module {
        std::uintptr_t base{};
        std::string path;
    };

    [[nodiscard]] std::vector<mapping> read_process_maps(pid_t pid,
                                                         std::string &error);
    [[nodiscard]] bool module_path_matches(const std::string &path,
                                           const std::string &selector);
    [[nodiscard]] std::optional<module>
    find_executable_module(pid_t pid, const std::string &selector,
                           std::string &error);
    [[nodiscard]] std::vector<mapping>
    find_module_mappings(pid_t pid, const std::string &selector,
                         bool executable_only, std::string &error);

} // namespace hdtool::memory
