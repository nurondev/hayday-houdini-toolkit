#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "memory/proc_maps.hpp"

namespace hdtool::memory {

    class process_memory {
      public:
        process_memory(pid_t pid, bool writable, std::string &error);
        ~process_memory();

        process_memory(const process_memory &) = delete;
        process_memory &operator=(const process_memory &) = delete;

        [[nodiscard]] bool valid() const;
        [[nodiscard]] bool read(std::uintptr_t address, void *buffer,
                                std::size_t length, std::string &error) const;
        [[nodiscard]] bool write(std::uintptr_t address, const void *buffer,
                                 std::size_t length, std::string &error) const;
        [[nodiscard]] bool dump(std::uintptr_t address, std::uint64_t length,
                                const std::string &output_path,
                                std::string &error) const;

      private:
        int descriptor_{-1};
    };

    using pattern_byte = std::optional<std::uint8_t>;

    [[nodiscard]] bool parse_hex_bytes(const std::string &text,
                                       std::vector<std::uint8_t> &bytes,
                                       std::string &error);
    [[nodiscard]] bool parse_pattern(const std::string &text,
                                     std::vector<pattern_byte> &pattern,
                                     std::string &error);
    [[nodiscard]] std::string format_hex(const std::uint8_t *bytes,
                                         std::size_t length);
    [[nodiscard]] std::vector<std::uintptr_t>
    scan_mapping(const process_memory &memory, const mapping &mapping,
                 const std::vector<pattern_byte> &pattern, std::string &error);
    [[nodiscard]] bool
    install_arm64_absolute_jump(const process_memory &memory,
                                std::uintptr_t address, std::uintptr_t target,
                                const std::vector<std::uint8_t> &expected,
                                std::string &error);

} // namespace hdtool::memory
