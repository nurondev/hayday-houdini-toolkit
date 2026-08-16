#include <stddef.h>
#include <stdint.h>

namespace {

constexpr uintptr_t kOutboundFrameRva = 0x1100A78;
constexpr uintptr_t kInboundDispatchRva = 0x0ACD804;
constexpr size_t kPageSize = 4096;
constexpr size_t kWorkerStackSize = 256 * 1024;
constexpr size_t kRecordBufferSize = 1024 * 1024;
constexpr size_t kMaxCapturedBody = 384 * 1024;

constexpr char kCapturePath[] = "/data/local/tmp/hayday-debug/packets.jsonl";
constexpr char kStatusPath[] = "/data/local/tmp/hayday-debug/status.txt";
constexpr char kLibgBasePath[] = "/data/local/tmp/hayday-debug/libg-base.txt";
constexpr char kHookPlanPath[] = "/data/local/tmp/hayday-debug/hook-plan.txt";
constexpr char kHooksReadyPath[] = "/data/local/tmp/hayday-debug/hooks-ready.flag";

constexpr uint8_t kOutboundPrologue[16] = {
        0xFF, 0x43, 0x02, 0xD1, 0xFD, 0x7B, 0x03, 0xA9,
        0xFB, 0x23, 0x00, 0xF9, 0xFA, 0x67, 0x05, 0xA9};
constexpr uint8_t kInboundPrologue[16] = {
        0xFF, 0xC3, 0x04, 0xD1, 0xFD, 0x7B, 0x0D, 0xA9,
        0xFC, 0x6F, 0x0E, 0xA9, 0xFA, 0x67, 0x0F, 0xA9};
constexpr long kAtFdcwd = -100;
constexpr long kOpenWriteOnly = 1;
constexpr long kOpenCreate = 0100;
constexpr long kOpenTruncate = 01000;
constexpr long kOpenAppend = 02000;
constexpr long kProtReadWriteExec = 7;
constexpr long kMapPrivateAnonymous = 0x22;
constexpr unsigned long kCloneThreadFlags =
        0x00000100UL | 0x00000200UL | 0x00000400UL | 0x00000800UL
        | 0x00010000UL | 0x00040000UL;

using OutboundFrameFunction = int64_t (*)(void *, void *);
using InboundDispatchFunction = void (*)(void *, void *);
using GetMessageTypeFunction = uint32_t (*)(void *);
OutboundFrameFunction g_outbound_original = nullptr;
InboundDispatchFunction g_inbound_original = nullptr;
char *g_record_buffer = nullptr;
int g_capture_fd = -1;
volatile uint32_t g_capture_lock = 0;
uint64_t g_sequence = 0;

long syscall1(long number, long argument1) {
    register long x0 asm("x0") = argument1;
    register long x8 asm("x8") = number;
    asm volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

long syscall2(long number, long argument1, long argument2) {
    register long x0 asm("x0") = argument1;
    register long x1 asm("x1") = argument2;
    register long x8 asm("x8") = number;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x8) : "memory");
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

long syscall4(long number, long argument1, long argument2, long argument3,
              long argument4) {
    register long x0 asm("x0") = argument1;
    register long x1 asm("x1") = argument2;
    register long x2 asm("x2") = argument3;
    register long x3 asm("x3") = argument4;
    register long x8 asm("x8") = number;
    asm volatile("svc #0" : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x8) : "memory");
    return x0;
}

long syscall6(long number, long argument1, long argument2, long argument3,
              long argument4, long argument5, long argument6) {
    register long x0 asm("x0") = argument1;
    register long x1 asm("x1") = argument2;
    register long x2 asm("x2") = argument3;
    register long x3 asm("x3") = argument4;
    register long x4 asm("x4") = argument5;
    register long x5 asm("x5") = argument6;
    register long x8 asm("x8") = number;
    asm volatile("svc #0" : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x8)
                 : "memory");
    return x0;
}

void *map_memory(size_t size, long protection) {
    const long result = syscall6(222, 0, (long)size, protection,
                                 kMapPrivateAnonymous, -1, 0);
    return result < 0 ? nullptr : reinterpret_cast<void *>(result);
}

size_t text_length(const char *value) {
    size_t result = 0;
    while (value[result] != '\0') ++result;
    return result;
}

bool bytes_equal(const void *left, const void *right, size_t length) {
    const auto *a = static_cast<const uint8_t *>(left);
    const auto *b = static_cast<const uint8_t *>(right);
    for (size_t index = 0; index < length; ++index) {
        if (a[index] != b[index]) return false;
    }
    return true;
}

void copy_bytes(void *destination, const void *source, size_t length) {
    auto *out = static_cast<uint8_t *>(destination);
    const auto *in = static_cast<const uint8_t *>(source);
    for (size_t index = 0; index < length; ++index) out[index] = in[index];
}

void write_all(int descriptor, const char *data, size_t length) {
    while (length != 0) {
        const long result = syscall3(64, descriptor, (long)data, (long)length);
        if (result <= 0) return;
        data += result;
        length -= (size_t)result;
    }
}

void write_status(const char *status) {
    const long descriptor = syscall4(56, kAtFdcwd, (long)kStatusPath,
                                     kOpenWriteOnly | kOpenCreate | kOpenTruncate,
                                     0666);
    if (descriptor < 0) return;
    write_all((int)descriptor, status, text_length(status));
    syscall1(57, descriptor);
}

void sleep_20ms() {
    const long interval[2] = {0, 20 * 1000 * 1000};
    syscall2(101, (long)interval, 0);
}

uintptr_t parse_hex(const char *value) {
    uintptr_t result = 0;
    for (;;) {
        const char character = *value++;
        uint32_t digit = 0;
        if (character >= '0' && character <= '9') digit = character - '0';
        else if (character >= 'a' && character <= 'f') digit = character - 'a' + 10;
        else if (character >= 'A' && character <= 'F') digit = character - 'A' + 10;
        else break;
        result = (result << 4U) | digit;
    }
    return result;
}

uintptr_t read_libg_base() {
    const long descriptor = syscall4(56, kAtFdcwd, (long)kLibgBasePath, 0, 0);
    if (descriptor < 0) return 0;
    char value[32];
    const long used = syscall3(63, descriptor, (long)value, sizeof(value) - 1);
    syscall1(57, descriptor);
    if (used <= 0) return 0;
    value[used] = '\0';
    const uintptr_t candidate = parse_hex(value);
    return candidate >= kPageSize && (candidate & (kPageSize - 1)) == 0
            ? candidate : 0;
}

void emit_absolute_jump(void *destination, const void *target) {
    auto *words = static_cast<uint32_t *>(destination);
    words[0] = 0x58000051U; // ldr x17, #8
    words[1] = 0xD61F0220U; // br x17
    *reinterpret_cast<uint64_t *>(words + 2) = reinterpret_cast<uint64_t>(target);
}

bool install_hook(void *target, void *replacement, void **original,
                  uint8_t *trampoline) {
    (void)replacement;
    copy_bytes(trampoline, target, 16);
    emit_absolute_jump(trampoline + 16, static_cast<uint8_t *>(target) + 16);
    *original = trampoline;
    return true;
}

void lock_capture() {
    uint32_t value;
    do {
        asm volatile("ldaxr %w0, [%1]" : "=&r"(value) : "r"(&g_capture_lock) : "memory");
        if (value != 0) continue;
        uint32_t failed;
        asm volatile("stxr %w0, %w2, [%1]"
                     : "=&r"(failed) : "r"(&g_capture_lock), "r"(1U) : "memory");
        if (failed == 0) return;
    } while (true);
}

void unlock_capture() {
    asm volatile("stlr wzr, [%0]" : : "r"(&g_capture_lock) : "memory");
}

char *append_text(char *cursor, char *end, const char *value) {
    while (*value != '\0' && cursor < end) *cursor++ = *value++;
    return cursor;
}

char *append_u64(char *cursor, char *end, uint64_t value) {
    char digits[24];
    size_t count = 0;
    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && count < sizeof(digits));
    while (count != 0 && cursor < end) *cursor++ = digits[--count];
    return cursor;
}

char *append_hex_address(char *cursor, char *end, uintptr_t value) {
    static constexpr char kHex[] = "0123456789abcdef";
    bool emitted = false;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const uint8_t digit = (uint8_t)((value >> shift) & 15U);
        if (!emitted && digit == 0 && shift != 0) continue;
        emitted = true;
        if (cursor < end) *cursor++ = kHex[digit];
    }
    return cursor;
}

bool write_hook_plan(void *outbound_target, void *outbound_proxy_address,
                     void *inbound_target, void *inbound_proxy_address) {
    char plan[512];
    char *cursor = plan;
    char *const end = plan + sizeof(plan);
    cursor = append_text(cursor, end, "out_target=");
    cursor = append_hex_address(cursor, end, (uintptr_t)outbound_target);
    cursor = append_text(cursor, end, "\nout_proxy=");
    cursor = append_hex_address(cursor, end, (uintptr_t)outbound_proxy_address);
    cursor = append_text(cursor, end, "\nin_target=");
    cursor = append_hex_address(cursor, end, (uintptr_t)inbound_target);
    cursor = append_text(cursor, end, "\nin_proxy=");
    cursor = append_hex_address(cursor, end, (uintptr_t)inbound_proxy_address);
    cursor = append_text(cursor, end, "\n");

    const long descriptor = syscall4(56, kAtFdcwd, (long)kHookPlanPath,
            kOpenWriteOnly | kOpenCreate | kOpenTruncate, 0666);
    if (descriptor < 0) return false;
    write_all((int)descriptor, plan, (size_t)(cursor - plan));
    syscall1(57, descriptor);
    return true;
}

bool hooks_ready_flag_exists() {
    const long descriptor = syscall4(56, kAtFdcwd, (long)kHooksReadyPath, 0, 0);
    if (descriptor < 0) return false;
    syscall1(57, descriptor);
    return true;
}

uint32_t message_type(void *message) {
    if (message == nullptr) return 0;
    auto **vtable = *reinterpret_cast<void ***>(message);
    if (vtable == nullptr || vtable[5] == nullptr) return 0;
    return reinterpret_cast<GetMessageTypeFunction>(vtable[5])(message);
}

uint32_t message_version(void *message) {
    return message == nullptr ? 0 : *reinterpret_cast<uint32_t *>(
            static_cast<uint8_t *>(message) + 136);
}

void capture_message(bool outbound, void *message, uint32_t known_type = 0,
                     uint32_t known_version = 0) {
    if (message == nullptr || g_capture_fd < 0 || g_record_buffer == nullptr) return;
    lock_capture();

    auto *base = static_cast<uint8_t *>(message);
    const uint32_t type = known_type != 0 ? known_type : message_type(message);
    const uint32_t version = known_version != 0 ? known_version : message_version(message);
    const bool redacted = type == 10101 || type == 25220;
    const uint32_t cursor_length = *reinterpret_cast<uint32_t *>(base + 28);
    const uint32_t readable_length = *reinterpret_cast<uint32_t *>(base + 32);
    const uint32_t body_length = outbound ? cursor_length : readable_length;
    const auto *body = *reinterpret_cast<const uint8_t **>(base + 64);
    size_t captured = redacted || body == nullptr ? 0 : body_length;
    if (captured > kMaxCapturedBody) captured = kMaxCapturedBody;
    const bool truncated = captured != 0 && captured != body_length;

    char *out = g_record_buffer;
    char *const end = g_record_buffer + kRecordBufferSize - 2;
    out = append_text(out, end, "{\"seq\":");
    out = append_u64(out, end, ++g_sequence);
    out = append_text(out, end, outbound
            ? ",\"direction\":\"out\",\"type\":"
            : ",\"direction\":\"in\",\"type\":");
    out = append_u64(out, end, type);
    out = append_text(out, end, ",\"version\":");
    out = append_u64(out, end, version);
    out = append_text(out, end, ",\"length\":");
    out = append_u64(out, end, body_length);
    out = append_text(out, end, redacted
            ? ",\"redacted\":true" : ",\"redacted\":false");
    out = append_text(out, end, truncated
            ? ",\"truncated\":true,\"payload_hex\":\""
            : ",\"truncated\":false,\"payload_hex\":\"");
    static constexpr char kHex[] = "0123456789abcdef";
    for (size_t index = 0; index < captured && out + 2 <= end; ++index) {
        *out++ = kHex[body[index] >> 4U];
        *out++ = kHex[body[index] & 15U];
    }
    out = append_text(out, end, "\"}\n");
    write_all(g_capture_fd, g_record_buffer, (size_t)(out - g_record_buffer));
    unlock_capture();
}

int64_t outbound_proxy(void *messaging, void *message) {
    const uint32_t type = message_type(message);
    const uint32_t version = message_version(message);
    const int64_t result = g_outbound_original(messaging, message);
    capture_message(true, message, type, version);
    return result;
}

void inbound_proxy(void *manager, void *message) {
    capture_message(false, message);
    g_inbound_original(manager, message);
}

void hook_worker() {
    write_status("stage: ARM64 hook worker started\n");
    uintptr_t base = 0;
    bool signatures_ready = false;
    for (int attempt = 0; attempt < 750; ++attempt) {
        base = read_libg_base();
        if (base != 0
            && bytes_equal(reinterpret_cast<void *>(base + kOutboundFrameRva),
                           kOutboundPrologue, 16)
            && bytes_equal(reinterpret_cast<void *>(base + kInboundDispatchRva),
                           kInboundPrologue, 16)) {
            signatures_ready = true;
            break;
        }
        sleep_20ms();
    }
    if (base == 0) {
        write_status("error: root helper did not publish the ARM64 libg base\n");
        return;
    }
    if (!signatures_ready) {
        write_status("error: decrypted Hay Day 1.72.84 hook signatures did not appear\n");
        return;
    }

    auto *trampolines = static_cast<uint8_t *>(map_memory(kPageSize, 7));
    g_record_buffer = static_cast<char *>(map_memory(kRecordBufferSize, 3));
    g_capture_fd = (int)syscall4(56, kAtFdcwd, (long)kCapturePath,
            kOpenWriteOnly | kOpenCreate | kOpenAppend, 0666);
    if (trampolines == nullptr || g_record_buffer == nullptr || g_capture_fd < 0) {
        write_status("error: logger allocation/open failed\n");
        return;
    }
    write_status("stage: decrypted signatures ready; preparing trampolines\n");

    void *outbound_target = reinterpret_cast<void *>(base + kOutboundFrameRva);
    void *inbound_target = reinterpret_cast<void *>(base + kInboundDispatchRva);
    const bool outbound_ok = install_hook(outbound_target,
            reinterpret_cast<void *>(outbound_proxy),
            reinterpret_cast<void **>(&g_outbound_original), trampolines);
    write_status(outbound_ok
            ? "stage: outbound trampoline prepared; preparing inbound\n"
            : "error: outbound ARM64 trampoline preparation failed\n");
    if (!outbound_ok) return;
    const bool inbound_ok = install_hook(inbound_target,
            reinterpret_cast<void *>(inbound_proxy),
            reinterpret_cast<void **>(&g_inbound_original), trampolines + 64);
    if (!inbound_ok) {
        write_status("error: inbound ARM64 trampoline preparation failed\n");
        return;
    }
    if (!write_hook_plan(outbound_target, reinterpret_cast<void *>(outbound_proxy),
                         inbound_target, reinterpret_cast<void *>(inbound_proxy))) {
        write_status("error: could not publish root hook plan\n");
        return;
    }
    write_status("stage: hook plan published; waiting for root branch writes\n");
    for (int attempt = 0; attempt < 1500; ++attempt) {
        if (hooks_ready_flag_exists()) {
            write_status("ready: Houdini ARM64 hooks installed for Hay Day 1.72.84\n");
            return;
        }
        sleep_20ms();
    }
    write_status("error: root helper did not confirm target branch writes\n");
    return;
}

extern "C" long hd_clone_thread(unsigned long flags, void *stack,
                                void (*entry)());

__attribute__((constructor)) void initialize() {
    write_status("stage: ARM64 payload constructor entered\n");
    void *stack = map_memory(kWorkerStackSize, 3);
    if (stack == nullptr) {
        write_status("error: worker stack allocation failed\n");
        return;
    }
    void *top = static_cast<uint8_t *>(stack) + kWorkerStackSize;
    const long result = hd_clone_thread(kCloneThreadFlags, top, hook_worker);
    if (result < 0) write_status("error: clone worker failed\n");
}

} // namespace

asm(R"(
.text
.align 4
.global hd_clone_thread
.type hd_clone_thread, %function
hd_clone_thread:
    sub x1, x1, #16
    str x2, [x1]
    mov x2, xzr
    mov x3, xzr
    mov x4, xzr
    mov x8, #220
    svc #0
    cbnz x0, 1f
    ldr x9, [sp]
    blr x9
    mov x10, #1
    str x10, [sp]
    str xzr, [sp, #8]
2:
    mov x0, sp
    mov x1, xzr
    mov x8, #101
    svc #0
    b 2b
1:
    ret
.size hd_clone_thread, .-hd_clone_thread
)");
