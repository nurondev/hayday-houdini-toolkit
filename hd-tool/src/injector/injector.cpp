#include "injector/injector.hpp"

#include <sys/ptrace.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <memory>
#include <sstream>
#include <vector>

#include "memory/proc_maps.hpp"

namespace hdtool::injector {
    namespace {

        std::string system_error(const std::string &operation) {
            return operation + ": " + std::strerror(errno);
        }

        bool peek_word(pid_t pid, std::uintptr_t address, unsigned long &value,
                       std::string &error) {
            errno = 0;
            const auto result =
                ptrace(PTRACE_PEEKDATA, pid, reinterpret_cast<void *>(address),
                       nullptr);
            if (result == -1 && errno != 0) {
                error = system_error("PTRACE_PEEKDATA");
                return false;
            }
            value = static_cast<unsigned long>(result);
            return true;
        }

        bool poke_word(pid_t pid, std::uintptr_t address, unsigned long value,
                       std::string &error) {
            if (ptrace(PTRACE_POKEDATA, pid, reinterpret_cast<void *>(address),
                       reinterpret_cast<void *>(value)) == -1) {
                error = system_error("PTRACE_POKEDATA");
                return false;
            }
            return true;
        }

        bool write_remote(pid_t pid, std::uintptr_t address, const void *source,
                          std::size_t length, std::string &error) {
            const auto *bytes = static_cast<const std::uint8_t *>(source);
            std::size_t offset = 0;
            while (offset < length) {
                unsigned long word = 0;
                const auto count = std::min(sizeof(word), length - offset);
                if (count != sizeof(word) &&
                    !peek_word(pid, address + offset, word, error))
                    return false;
                std::memcpy(&word, bytes + offset, count);
                if (!poke_word(pid, address + offset, word, error))
                    return false;
                offset += count;
            }
            return true;
        }

        bool wait_stopped(pid_t pid, int &signal, std::string &error) {
            int status = 0;
            if (waitpid(pid, &status, __WALL) == -1) {
                error = system_error("waitpid");
                return false;
            }
            if (!WIFSTOPPED(status)) {
                std::ostringstream message;
                message << "target did not stop (status=0x" << std::hex
                        << status << ')';
                error = message.str();
                return false;
            }
            signal = WSTOPSIG(status);
            return true;
        }

        class ptrace_session {
          public:
            explicit ptrace_session(pid_t pid) : pid_(pid) {}

            bool attach(std::string &error) {
                if (ptrace(PTRACE_ATTACH, pid_, nullptr, nullptr) == -1) {
                    error = system_error("PTRACE_ATTACH");
                    return false;
                }
                attached_ = true;
                int signal = 0;
                return wait_stopped(pid_, signal, error);
            }

            ~ptrace_session() {
                if (attached_)
                    ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
            }

          private:
            pid_t pid_{};
            bool attached_{false};
        };

        bool intercept_next_load(pid_t pid, std::uintptr_t function,
                                 const std::string &guest_path,
                                 std::uintptr_t &handle, std::string &error) {
            unsigned long saved_entry = 0;
            if (!peek_word(pid, function, saved_entry, error))
                return false;
            if (!poke_word(pid, function, (saved_entry & ~0xffUL) | 0xccUL,
                           error))
                return false;

            if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1) {
                error = system_error("PTRACE_CONT to bridge loader");
                std::string ignored;
                poke_word(pid, function, saved_entry, ignored);
                return false;
            }

            int stop_signal = 0;
            if (!wait_stopped(pid, stop_signal, error))
                return false;
            user_regs_struct original{};
            if (ptrace(PTRACE_GETREGS, pid, nullptr, &original) == -1) {
                error = system_error("PTRACE_GETREGS at bridge loader");
                return false;
            }
            if (stop_signal != SIGTRAP || original.rip != function + 1) {
                std::ostringstream message;
                message << "unexpected bridge-loader stop: signal="
                        << stop_signal << " rip=0x" << std::hex << original.rip;
                error = message.str();
                std::string ignored;
                poke_word(pid, function, saved_entry, ignored);
                return false;
            }
            if (!poke_word(pid, function, saved_entry, error))
                return false;
            original.rip = function;

            unsigned long caller_return = 0;
            if (!peek_word(pid, original.rsp, caller_return, error))
                return false;
            unsigned long saved_caller_code = 0;
            if (!peek_word(pid, caller_return, saved_caller_code, error))
                return false;
            if (!poke_word(pid, caller_return,
                           (saved_caller_code & ~0xffUL) | 0xccUL, error))
                return false;

            const std::size_t path_length = guest_path.size() + 1;
            const std::size_t word_count =
                (path_length + sizeof(unsigned long) - 1) /
                sizeof(unsigned long);
            const std::uintptr_t remote_path = original.rsp - 0x8000UL;
            std::vector<unsigned long> saved_path(word_count);
            bool path_saved = true;
            for (std::size_t index = 0; index < word_count; ++index) {
                if (!peek_word(pid, remote_path + index * sizeof(unsigned long),
                               saved_path[index], error)) {
                    path_saved = false;
                    break;
                }
            }

            bool succeeded = false;
            if (path_saved && write_remote(pid, remote_path, guest_path.c_str(),
                                           path_length, error)) {
                user_regs_struct replacement = original;
                replacement.rdi = remote_path;
                // RSI and RDX deliberately retain the intercepted call's flags
                // and NativeBridgeNamespace pointer.
                if (ptrace(PTRACE_SETREGS, pid, nullptr, &replacement) == -1) {
                    error = system_error("PTRACE_SETREGS for replacement load");
                } else if (ptrace(PTRACE_CONT, pid, nullptr, nullptr) == -1) {
                    error = system_error("PTRACE_CONT replacement load");
                } else if (wait_stopped(pid, stop_signal, error) &&
                           ptrace(PTRACE_GETREGS, pid, nullptr, &replacement) !=
                               -1) {
                    if (stop_signal == SIGTRAP &&
                        replacement.rip == caller_return + 1) {
                        handle = replacement.rax;
                        succeeded = handle != 0;
                        if (!succeeded)
                            error = "native bridge returned a null handle";
                    } else {
                        std::ostringstream message;
                        message
                            << "replacement load stopped unexpectedly: signal="
                            << stop_signal << " rip=0x" << std::hex
                            << replacement.rip;
                        error = message.str();
                    }
                } else if (error.empty()) {
                    error =
                        system_error("PTRACE_GETREGS after replacement load");
                }
            }

            if (ptrace(PTRACE_SETREGS, pid, nullptr, &original) == -1 &&
                error.empty())
                error = system_error("restoring intercepted registers");
            if (path_saved) {
                std::string ignored;
                for (std::size_t index = 0; index < word_count; ++index)
                    poke_word(pid, remote_path + index * sizeof(unsigned long),
                              saved_path[index], ignored);
            }
            {
                std::string ignored;
                if (!poke_word(pid, caller_return, saved_caller_code,
                               ignored) &&
                    error.empty())
                    error = ignored;
            }
            return succeeded;
        }

    } // namespace

    bool inject_guest_library(const inject_options &options,
                              inject_result &result, std::string &error) {
#if !defined(__x86_64__)
        error = "the current ptrace injector backend requires x86_64";
        return false;
#else
        if (options.pid <= 0 || options.library_path.empty()) {
            error = "inject requires a positive PID and guest library path";
            return false;
        }
        if (options.bridge_address.has_value() ==
            options.bridge_rva.has_value()) {
            error = "provide exactly one of bridge address or bridge RVA";
            return false;
        }

        if (options.bridge_address.has_value()) {
            result.loader_address = options.bridge_address.value();
        } else {
            const auto module = memory::find_executable_module(
                options.pid, options.bridge_module, error);
            if (!module.has_value())
                return false;
            result.bridge_base = module->base;
            result.loader_address = module->base + options.bridge_rva.value();
        }

        ptrace_session session(options.pid);
        if (!session.attach(error))
            return false;
        return intercept_next_load(options.pid, result.loader_address,
                                   options.library_path, result.handle, error);
#endif
    }

} // namespace hdtool::injector
