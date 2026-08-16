#include <stdint.h>

namespace {

constexpr long kAtFdcwd = -100;
constexpr long kOpenWriteOnly = 1;
constexpr long kOpenCreate = 0100;
constexpr long kOpenTruncate = 01000;

long syscall1(long number, long argument1) {
    register long x0 asm("x0") = argument1;
    register long x8 asm("x8") = number;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

long syscall3(long number, long argument1, long argument2, long argument3) {
    register long x0 asm("x0") = argument1;
    register long x1 asm("x1") = argument2;
    register long x2 asm("x2") = argument3;
    register long x8 asm("x8") = number;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

__attribute__((constructor)) void guest_loaded() {
    static constexpr char kPath[] =
            "/data/local/tmp/hayday-debug/houdini-arm64-loaded.txt";
    static constexpr char kMarker[] =
            "ARM64 guest constructor executed through Houdini\n";
    const long descriptor = syscall3(56, kAtFdcwd, (long)kPath,
                                     kOpenWriteOnly | kOpenCreate | kOpenTruncate);
    if (descriptor < 0) return;
    syscall3(64, descriptor, (long)kMarker, sizeof(kMarker) - 1);
    syscall1(57, descriptor);
}

} // namespace
