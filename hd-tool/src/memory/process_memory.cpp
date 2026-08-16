#include "memory/process_memory.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hdtool::memory {
    namespace {

        int hex_value(char value) {
            if (value >= '0' && value <= '9')
                return value - '0';
            if (value >= 'a' && value <= 'f')
                return value - 'a' + 10;
            if (value >= 'A' && value <= 'F')
                return value - 'A' + 10;
            return -1;
        }

    } // namespace

    process_memory::process_memory(pid_t pid, bool writable,
                                   std::string &error) {
        const std::string path = "/proc/" + std::to_string(pid) + "/mem";
        descriptor_ = open(path.c_str(), writable ? O_RDWR : O_RDONLY);
        if (descriptor_ < 0)
            error = "could not open " + path + ": " + std::strerror(errno);
    }

    process_memory::~process_memory() {
        if (descriptor_ >= 0)
            close(descriptor_);
    }

    bool process_memory::valid() const { return descriptor_ >= 0; }

    bool process_memory::read(std::uintptr_t address, void *buffer,
                              std::size_t length, std::string &error) const {
        auto *output = static_cast<std::uint8_t *>(buffer);
        std::size_t completed = 0;
        while (completed < length) {
            const auto result =
                pread(descriptor_, output + completed, length - completed,
                      static_cast<off_t>(address + completed));
            if (result <= 0) {
                error = "process memory read failed at 0x";
                std::ostringstream address_stream;
                address_stream << std::hex << address + completed;
                error += address_stream.str() + ": " + std::strerror(errno);
                return false;
            }
            completed += static_cast<std::size_t>(result);
        }
        return true;
    }

    bool process_memory::write(std::uintptr_t address, const void *buffer,
                               std::size_t length, std::string &error) const {
        const auto *input = static_cast<const std::uint8_t *>(buffer);
        std::size_t completed = 0;
        while (completed < length) {
            const auto result =
                pwrite(descriptor_, input + completed, length - completed,
                       static_cast<off_t>(address + completed));
            if (result <= 0) {
                error = "process memory write failed: ";
                error += std::strerror(errno);
                return false;
            }
            completed += static_cast<std::size_t>(result);
        }
        return true;
    }

    bool process_memory::dump(std::uintptr_t address, std::uint64_t length,
                              const std::string &output_path,
                              std::string &error) const {
        std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
        if (!output) {
            error = "could not create dump: " + output_path;
            return false;
        }
        std::array<std::uint8_t, 64 * 1024> buffer{};
        std::uint64_t completed = 0;
        while (completed < length) {
            const auto count = static_cast<std::size_t>(
                std::min<std::uint64_t>(buffer.size(), length - completed));
            if (!read(address + static_cast<std::uintptr_t>(completed),
                      buffer.data(), count, error))
                return false;
            output.write(reinterpret_cast<const char *>(buffer.data()),
                         static_cast<std::streamsize>(count));
            if (!output) {
                error = "could not write dump: " + output_path;
                return false;
            }
            completed += count;
        }
        return true;
    }

    bool parse_hex_bytes(const std::string &text,
                         std::vector<std::uint8_t> &bytes, std::string &error) {
        if (text.empty() || (text.size() % 2) != 0) {
            error = "hex bytes must contain an even, nonzero number of digits";
            return false;
        }
        bytes.clear();
        bytes.reserve(text.size() / 2);
        for (std::size_t index = 0; index < text.size(); index += 2) {
            const int high = hex_value(text[index]);
            const int low = hex_value(text[index + 1]);
            if (high < 0 || low < 0) {
                error = "invalid hexadecimal byte string";
                return false;
            }
            bytes.push_back(static_cast<std::uint8_t>((high << 4) | low));
        }
        return true;
    }

    bool parse_pattern(const std::string &text,
                       std::vector<pattern_byte> &pattern, std::string &error) {
        std::istringstream tokens(text);
        std::string token;
        pattern.clear();
        while (tokens >> token) {
            if (token == "?" || token == "??") {
                pattern.emplace_back(std::nullopt);
                continue;
            }
            std::vector<std::uint8_t> byte;
            if (token.size() != 2 || !parse_hex_bytes(token, byte, error)) {
                error = "patterns use space-separated bytes or ?? wildcards";
                return false;
            }
            pattern.emplace_back(byte.front());
        }
        if (pattern.empty()) {
            error = "pattern must not be empty";
            return false;
        }
        return true;
    }

    std::string format_hex(const std::uint8_t *bytes, std::size_t length) {
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (std::size_t index = 0; index < length; ++index)
            output << std::setw(2) << static_cast<unsigned int>(bytes[index]);
        return output.str();
    }

    std::vector<std::uintptr_t>
    scan_mapping(const process_memory &memory, const mapping &mapping,
                 const std::vector<pattern_byte> &pattern, std::string &error) {
        std::vector<std::uintptr_t> matches;
        if (pattern.empty() || mapping.size() < pattern.size())
            return matches;

        constexpr std::size_t chunk_size = 256 * 1024;
        const std::size_t overlap = pattern.size() - 1;
        std::vector<std::uint8_t> buffer(chunk_size + overlap);
        std::uintptr_t cursor = mapping.start;
        std::size_t carried = 0;
        while (cursor < mapping.end) {
            const auto remaining =
                static_cast<std::size_t>(mapping.end - cursor);
            const std::size_t requested = std::min(chunk_size, remaining);
            if (!memory.read(cursor, buffer.data() + carried, requested, error))
                return {};
            const std::size_t available = carried + requested;
            const std::uintptr_t buffer_base = cursor - carried;
            for (std::size_t index = 0; index + pattern.size() <= available;
                 ++index) {
                bool matched = true;
                for (std::size_t byte = 0; byte < pattern.size(); ++byte) {
                    if (pattern[byte].has_value() &&
                        buffer[index + byte] != pattern[byte].value()) {
                        matched = false;
                        break;
                    }
                }
                if (matched)
                    matches.push_back(buffer_base + index);
            }
            carried = std::min(overlap, available);
            if (carried != 0)
                std::memmove(buffer.data(), buffer.data() + available - carried,
                             carried);
            cursor += requested;
        }
        return matches;
    }

    bool install_arm64_absolute_jump(const process_memory &memory,
                                     std::uintptr_t address,
                                     std::uintptr_t target,
                                     const std::vector<std::uint8_t> &expected,
                                     std::string &error) {
        if (expected.size() != 16) {
            error = "ARM64 absolute jumps require a 16-byte expected signature";
            return false;
        }
        std::array<std::uint8_t, 16> current{};
        if (!memory.read(address, current.data(), current.size(), error))
            return false;
        if (!std::equal(current.begin(), current.end(), expected.begin())) {
            error = "signature mismatch at hook target";
            return false;
        }

        std::array<std::uint8_t, 16> patch{};
        constexpr std::uint32_t load_x17_literal = 0x58000051U;
        constexpr std::uint32_t branch_x17 = 0xD61F0220U;
        std::memcpy(patch.data(), &load_x17_literal, sizeof(load_x17_literal));
        std::memcpy(patch.data() + 4, &branch_x17, sizeof(branch_x17));
        const auto target64 = static_cast<std::uint64_t>(target);
        std::memcpy(patch.data() + 8, &target64, sizeof(target64));
        return memory.write(address, patch.data(), patch.size(), error);
    }

} // namespace hdtool::memory
