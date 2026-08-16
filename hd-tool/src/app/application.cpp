#include "app/application.hpp"

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "injector/injector.hpp"
#include "memory/proc_maps.hpp"
#include "memory/process_memory.hpp"

namespace {

    using options = std::map<std::string, std::string>;

    void print_usage(std::ostream &output) {
        output
            << "hd-tool - generic Houdini guest injector and process-memory "
               "utility\n\n"
               "Usage:\n"
               "  hd-tool inject --pid PID --library PATH --bridge-rva RVA "
               "[--bridge "
               "MODULE]\n"
               "  hd-tool inject --pid PID --library PATH --bridge-address "
               "ADDRESS\n"
               "  hd-tool read --pid PID --address ADDRESS [--length LENGTH]\n"
               "  hd-tool write --pid PID --address ADDRESS --bytes HEX\n"
               "  hd-tool dump --pid PID --address ADDRESS --output FILE "
               "--length "
               "LENGTH [--offset OFFSET]\n"
               "  hd-tool jump --pid PID --address ADDRESS --target ADDRESS "
               "--expected HEX16\n"
               "  hd-tool scan --pid PID --module MODULE --pattern \"AA BB ?? "
               "DD\"\n";
    }

    bool parse_options(int argc, char **argv, options &options,
                       std::string &error) {
        for (int index = 2; index < argc; index += 2) {
            const std::string name = argv[index];
            if (!name.starts_with("--") || index + 1 >= argc) {
                error = "options must use --name value pairs";
                return false;
            }
            if (!options.emplace(name.substr(2), argv[index + 1]).second) {
                error = "duplicate option: " + name;
                return false;
            }
        }
        return true;
    }

    const std::string *find_option(const options &options,
                                   const std::string &name) {
        const auto found = options.find(name);
        return found == options.end() ? nullptr : &found->second;
    }

    bool required_option(const options &options, const std::string &name,
                         std::string &value, std::string &error) {
        const auto *candidate = find_option(options, name);
        if (candidate == nullptr || candidate->empty()) {
            error = "missing required option --" + name;
            return false;
        }
        value = *candidate;
        return true;
    }

    bool parse_number(const std::string &text, std::uint64_t &value,
                      std::string &error) {
        errno = 0;
        char *end = nullptr;
        const auto parsed = std::strtoull(text.c_str(), &end, 0);
        if (errno != 0 || end == text.c_str() || *end != '\0') {
            error = "invalid integer: " + text;
            return false;
        }
        value = parsed;
        return true;
    }

    bool read_pid(const options &options, pid_t &pid, std::string &error) {
        std::string text;
        std::uint64_t value = 0;
        if (!required_option(options, "pid", text, error) ||
            !parse_number(text, value, error) || value == 0 ||
            value >
                static_cast<std::uint64_t>(std::numeric_limits<pid_t>::max())) {
            if (error.empty())
                error = "PID is outside the supported range";
            return false;
        }
        pid = static_cast<pid_t>(value);
        return true;
    }

    bool read_address(const options &options, const std::string &name,
                      std::uintptr_t &address, std::string &error) {
        std::string text;
        std::uint64_t value = 0;
        if (!required_option(options, name, text, error) ||
            !parse_number(text, value, error))
            return false;
        address = static_cast<std::uintptr_t>(value);
        return true;
    }

    int inject_command(const options &options, std::string &error) {
        hdtool::injector::inject_options request;
        if (!read_pid(options, request.pid, error) ||
            !required_option(options, "library", request.library_path, error))
            return 2;
        if (const auto *bridge = find_option(options, "bridge"))
            request.bridge_module = *bridge;

        if (const auto *rva = find_option(options, "bridge-rva")) {
            std::uint64_t value = 0;
            if (!parse_number(*rva, value, error))
                return 2;
            request.bridge_rva = static_cast<std::uintptr_t>(value);
        }
        if (const auto *address = find_option(options, "bridge-address")) {
            std::uint64_t value = 0;
            if (!parse_number(*address, value, error))
                return 2;
            request.bridge_address = static_cast<std::uintptr_t>(value);
        }

        hdtool::injector::inject_result result;
        if (!hdtool::injector::inject_guest_library(request, result, error))
            return 1;
        if (result.bridge_base != 0)
            std::cout << "bridge_base=0x" << std::hex << result.bridge_base
                      << '\n';
        std::cout << "loader_address=0x" << std::hex << result.loader_address
                  << '\n'
                  << "handle=0x" << result.handle << '\n';
        return 0;
    }

    int read_command(const options &options, std::string &error) {
        pid_t pid = 0;
        std::uintptr_t address = 0;
        if (!read_pid(options, pid, error) ||
            !read_address(options, "address", address, error))
            return 2;
        std::uint64_t length = 16;
        if (const auto *text = find_option(options, "length")) {
            if (!parse_number(*text, length, error))
                return 2;
        }
        if (length == 0 || length > 1024 * 1024) {
            error = "read length must be between 1 byte and 1 MiB";
            return 2;
        }
        hdtool::memory::process_memory memory(pid, false, error);
        if (!memory.valid())
            return 1;
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
        if (!memory.read(address, bytes.data(), bytes.size(), error))
            return 1;
        std::cout << hdtool::memory::format_hex(bytes.data(), bytes.size())
                  << '\n';
        return 0;
    }

    int write_command(const options &options, std::string &error) {
        pid_t pid = 0;
        std::uintptr_t address = 0;
        std::string text;
        if (!read_pid(options, pid, error) ||
            !read_address(options, "address", address, error) ||
            !required_option(options, "bytes", text, error))
            return 2;
        std::vector<std::uint8_t> bytes;
        if (!hdtool::memory::parse_hex_bytes(text, bytes, error))
            return 2;
        hdtool::memory::process_memory memory(pid, true, error);
        if (!memory.valid() ||
            !memory.write(address, bytes.data(), bytes.size(), error))
            return 1;
        std::cout << "wrote " << bytes.size() << " bytes at 0x" << std::hex
                  << address << '\n';
        return 0;
    }

    int dump_command(const options &options, std::string &error) {
        pid_t pid = 0;
        std::uintptr_t address = 0;
        std::string output;
        std::string length_text;
        std::uint64_t length = 0;
        if (!read_pid(options, pid, error) ||
            !read_address(options, "address", address, error) ||
            !required_option(options, "output", output, error) ||
            !required_option(options, "length", length_text, error) ||
            !parse_number(length_text, length, error))
            return 2;
        if (const auto *offset = find_option(options, "offset")) {
            std::uint64_t value = 0;
            if (!parse_number(*offset, value, error))
                return 2;
            address += static_cast<std::uintptr_t>(value);
        }
        if (length == 0) {
            error = "dump length must be nonzero";
            return 2;
        }
        hdtool::memory::process_memory memory(pid, false, error);
        if (!memory.valid() || !memory.dump(address, length, output, error))
            return 1;
        std::cout << "dumped 0x" << std::hex << length << " bytes from 0x"
                  << address << " to " << output << '\n';
        return 0;
    }

    int jump_command(const options &options, std::string &error) {
        pid_t pid = 0;
        std::uintptr_t address = 0;
        std::uintptr_t target = 0;
        std::string signature;
        if (!read_pid(options, pid, error) ||
            !read_address(options, "address", address, error) ||
            !read_address(options, "target", target, error) ||
            !required_option(options, "expected", signature, error))
            return 2;
        std::vector<std::uint8_t> expected;
        if (!hdtool::memory::parse_hex_bytes(signature, expected, error))
            return 2;
        hdtool::memory::process_memory memory(pid, true, error);
        if (!memory.valid() || !hdtool::memory::install_arm64_absolute_jump(
                                   memory, address, target, expected, error))
            return 1;
        std::cout << "installed ARM64 absolute jump 0x" << std::hex << address
                  << " -> 0x" << target << '\n';
        return 0;
    }

    int scan_command(const options &options, std::string &error) {
        pid_t pid = 0;
        std::string module;
        std::string pattern_text;
        if (!read_pid(options, pid, error) ||
            !required_option(options, "module", module, error) ||
            !required_option(options, "pattern", pattern_text, error))
            return 2;
        std::vector<hdtool::memory::pattern_byte> pattern;
        if (!hdtool::memory::parse_pattern(pattern_text, pattern, error))
            return 2;
        const auto mappings =
            hdtool::memory::find_module_mappings(pid, module, true, error);
        if (!error.empty())
            return 1;
        hdtool::memory::process_memory memory(pid, false, error);
        if (!memory.valid())
            return 1;
        std::size_t count = 0;
        for (const auto &mapping : mappings) {
            const auto matches =
                hdtool::memory::scan_mapping(memory, mapping, pattern, error);
            if (!error.empty())
                return 1;
            for (const auto address : matches) {
                std::cout << "0x" << std::hex << address << '\n';
                ++count;
            }
        }
        if (count == 0)
            return 1;
        return 0;
    }

} // namespace

int hdtool::application::run(int argc, char **argv) {
    if (argc < 2 || std::string(argv[1]) == "help" ||
        std::string(argv[1]) == "--help") {
        print_usage(argc < 2 ? std::cerr : std::cout);
        return argc < 2 ? 2 : 0;
    }

    options options;
    std::string error;
    if (!parse_options(argc, argv, options, error)) {
        std::cerr << "error: " << error << '\n';
        return 2;
    }

    const std::string command = argv[1];
    int result = 2;
    if (command == "inject")
        result = inject_command(options, error);
    else if (command == "read")
        result = read_command(options, error);
    else if (command == "write")
        result = write_command(options, error);
    else if (command == "dump")
        result = dump_command(options, error);
    else if (command == "jump")
        result = jump_command(options, error);
    else if (command == "scan")
        result = scan_command(options, error);
    else
        error = "unknown command: " + command;

    if (result != 0 && !error.empty())
        std::cerr << "error: " << error << '\n';
    if (result == 2)
        print_usage(std::cerr);
    return result;
}
