#include "memory/proc_maps.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <sstream>

namespace hdtool::memory {
    namespace {

        std::string base_name(const std::string &path) {
            const auto slash = path.find_last_of('/');
            return slash == std::string::npos ? path : path.substr(slash + 1);
        }

        void trim_left(std::string &value) {
            const auto first = value.find_first_not_of(" \t");
            value = first == std::string::npos ? std::string{}
                                               : value.substr(first);
        }

    } // namespace

    bool mapping::executable() const {
        return permissions.find('x') != std::string::npos;
    }

    std::size_t mapping::size() const {
        return end > start ? static_cast<std::size_t>(end - start) : 0;
    }

    std::vector<mapping> read_process_maps(pid_t pid, std::string &error) {
        const std::string path = "/proc/" + std::to_string(pid) + "/maps";
        std::ifstream stream(path);
        if (!stream) {
            error = "could not open " + path + ": " + std::strerror(errno);
            return {};
        }

        std::vector<mapping> mappings;
        std::string line;
        while (std::getline(stream, line)) {
            std::istringstream fields(line);
            std::string range;
            std::string offset;
            std::string device;
            std::string inode;
            mapping mapping;
            if (!(fields >> range >> mapping.permissions >> offset >> device >>
                  inode))
                continue;
            std::getline(fields, mapping.path);
            trim_left(mapping.path);

            const auto dash = range.find('-');
            if (dash == std::string::npos)
                continue;
            try {
                mapping.start = std::stoull(range.substr(0, dash), nullptr, 16);
                mapping.end = std::stoull(range.substr(dash + 1), nullptr, 16);
                mapping.file_offset = std::stoull(offset, nullptr, 16);
            } catch (...) {
                continue;
            }
            mappings.push_back(std::move(mapping));
        }
        return mappings;
    }

    bool module_path_matches(const std::string &path,
                             const std::string &selector) {
        if (selector.find('/') != std::string::npos)
            return path == selector;
        if (path.find("/arm64/") != std::string::npos)
            return false;
        return base_name(path) == base_name(selector);
    }

    std::optional<module> find_executable_module(pid_t pid,
                                                 const std::string &selector,
                                                 std::string &error) {
        const auto mappings = read_process_maps(pid, error);
        if (!error.empty())
            return std::nullopt;
        for (const auto &mapping : mappings) {
            if (!mapping.executable() ||
                !module_path_matches(mapping.path, selector))
                continue;
            return module{mapping.start - mapping.file_offset, mapping.path};
        }
        error = "executable module is not mapped: " + selector;
        return std::nullopt;
    }

    std::vector<mapping> find_module_mappings(pid_t pid,
                                              const std::string &selector,
                                              bool executable_only,
                                              std::string &error) {
        const auto mappings = read_process_maps(pid, error);
        if (!error.empty())
            return {};
        std::vector<mapping> matches;
        std::copy_if(mappings.begin(), mappings.end(),
                     std::back_inserter(matches), [&](const mapping &mapping) {
                         return (!executable_only || mapping.executable()) &&
                                module_path_matches(mapping.path, selector);
                     });
        if (matches.empty())
            error = "module is not mapped: " + selector;
        return matches;
    }

} // namespace hdtool::memory
