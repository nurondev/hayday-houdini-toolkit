# Hay Day Houdini Toolkit

Houdini-native instrumentation toolkit for Hay Day 1.72.84 (code 2201) on
MEmu. It captures plaintext Piranha messages inside Hay Day after
decryption/before encryption and can dump the decrypted runtime `libg.so`.

This project was mostly vibe-coded, as I only needed access to the raw packets
to understand the game's network protocol. For reliable long-term support
across game versions, you should use deterministic pattern/AOB scanning as a
best practice. Lastly, the toolkit is only expected to work on MEmu running
Android 9.

## Purpose

This project is a small, extensible reference for loading ARM64 guest code into
an x86_64 Android application that runs through Houdini. It separates the
reusable device tooling from the application-specific payloads:

- `hd-tool` is a generic native-bridge injector and process-memory utility. It
  can inject a compatible guest library, inspect or dump memory, scan executable
  mappings, and apply externally supplied ARM64 jumps. It contains no Hay Day
  function addresses or message layouts.
- The guest libraries under `examples/` demonstrate how an injected payload can
  locate runtime state, prepare trampolines, publish a hook plan, and capture
  application data. Those examples are intentionally version-specific.

The current practical example observes Hay Day's native Piranha protocol at
plaintext boundaries. It is not an SSL interception project: outbound messages
are observed after encoding but before transport protection, and inbound
messages are observed after authentication, decryption, and decoding.

## Architecture

```text
Windows / WSL
├── Builds the two architecture-specific CMake projects
└── Uses Bash and ADB for deployment

Android emulator (x86_64)
├── hd-tool
│   ├── Discovers executable process mappings
│   ├── Intercepts NativeBridgeLoadLibraryExt with ptrace
│   ├── Preserves the intercepted linker namespace
│   └── Provides generic read, write, dump, scan, and jump commands
└── Hay Day x86_64 ART process
    └── Houdini
        └── ARM64 guest example
            ├── Waits for decrypted libg.so state
            ├── Prepares ARM64 trampolines
            ├── Publishes signature-checked hook addresses
            └── Records plaintext messages
```

The injector attaches early because Hay Day starts its own tracer during
startup. It places a temporary breakpoint on the host native-bridge loader,
substitutes the requested guest library for one legitimate load while retaining
the original flags and `NativeBridgeNamespace*`, and then restores the original
call. The guest constructor consequently executes through Houdini in the same
namespace as the application.

After `libg.so` reaches its stable decrypted mapping, the launcher verifies the
expected function prologues. The guest prepares trampolines and publishes their
runtime addresses; after the configured startup-integrity delay, `hd-tool`
applies the branches. Unknown signatures fail closed rather than modifying an
unrecognized build.

## Requirements

- A Houdini-based Android emulator with root ADB enabled
- Hay Day 1.72.84/code 2201 with the analyzed ARM64 `libg.so`
- CMake, a C++20 compiler, and Clang with an AArch64-capable LLD linker
- Bash for the build and deployment helpers
- ADB on `PATH`, or `ADB_PATH` set to an `adb`/`adb.exe` executable
- `just` for the documented root command interface (optional)

The toolkit intercepts the emulator's initialized native bridge before Hay Day's
anti-debug child attaches, loads a dependency-free ARM64 payload through
Houdini, and applies signature-checked branches after the startup integrity
scan. It does not require an Android companion app, Frida, Magisk, or LSPosed.

The tested target is MEmu/Android 9 with Houdini 9.0.7 and Hay Day
1.72.84/code 2201.

## Project layout

- `hd-tool/` is the generic x86_64 device application. It contains no Hay Day
  hook RVAs or message layouts.
- `examples/` contains the unchanged ARM64 guest payload examples, including
  the version-specific Hay Day hooks.
- `scripts/` contains build, deployment, and on-device orchestration helpers.
- `out/` contains generated executables and shared libraries.

There is intentionally no root `CMakeLists.txt`: `hd-tool` and `examples` are
configured independently because they target different architectures.

## Just recipes

The root `justfile` documents and exposes the build, deployment, collection,
and diagnostic workflows. Run `just` or `just --list` to see every recipe:

```bash
just
just build
just deploy-memu
just status
just listen-messages
```

Use `just scripts` to see the role of every file under `scripts/`, including
the packet listeners, shared host helper, and Android-only launcher. The
scripts remain directly usable when `just` is not installed.

## Generic `hd-tool` capabilities

The device-side CLI currently exposes:

```text
hd-tool inject --pid PID --library GUEST_SO \
    (--bridge-rva RVA [--bridge MODULE] | --bridge-address ADDRESS)
hd-tool read --pid PID --address ADDRESS [--length LENGTH]
hd-tool write --pid PID --address ADDRESS --bytes HEX
hd-tool dump --pid PID --address ADDRESS --output FILE --length LENGTH
hd-tool jump --pid PID --address ADDRESS --target ADDRESS --expected HEX16
hd-tool scan --pid PID --module MODULE --pattern "AA BB ?? DD"
```

Bridge selection is caller-supplied in the current scaffold. The wildcard
scanner can locate candidates, while future ELF-symbol or pattern resolvers can
produce `--bridge-address` without changing the injection backend.

## What it hooks

| Direction | RVA | Boundary |
|---|---:|---|
| Outbound | `0x1100A78` | After `PiranhaMessage::encode`, while its ByteStream is still plaintext |
| Inbound | `0x0ACD804` | MessageManager dispatch, after authentication, decryption, and decode |

Runtime addresses are `libg.so base + RVA`. The module verifies the first 16
bytes of both functions against the decrypted 1.72.84 binary before hooking. It
refuses to install hooks on a different build instead of risking a crash.

The PiranhaMessage/ByteStream layout used by the logger is:

- virtual `getType()` at vtable offset `+40`;
- message version at message offset `+136`;
- embedded ByteStream at message offset `+8`;
- ByteStream cursor at `+20`, readable length at `+24`, buffer at `+56`.

## Build

Build `hd-tool` and the ARM64 guest examples from WSL/Linux:

```text
bash scripts/build-probe.sh
```

The MEmu-compatible payload targets Android API 28, uses conventional RELA
relocations instead of RELR, and uses 16 KiB-compatible ELF segment alignment.
If `ld.lld` is not on `PATH`, set `LLD_PATH` to its executable:

```text
LLD_PATH=/path/to/ld.lld bash scripts/build-probe.sh
```

The two CMake projects can also be configured separately. See
`hd-tool/README.md` and `examples/README.md` for their direct commands.

## Run

If ADB is not already on `PATH`, define its executable in the current shell.
Windows `adb.exe` is supported from WSL:

```bash
export ADB_PATH=/mnt/c/path/to/platform-tools/adb.exe
```

Use ADB to list emulator serials, then deploy to the selected one. For MEmu,
the default serial is `127.0.0.1:21503`:

```bash
"${ADB_PATH:-adb}" devices
bash scripts/deploy-memu.sh
```

MEmu deployment is quiet by default and prints only `successfully injected`
after a successful run. Pass `--verbose` to display build staging, injection,
and device-side progress:

```bash
bash scripts/deploy-memu.sh --verbose
```

The MEmu profile selects the executable host bridge at
`/system/lib64/libnativebridge.so` and its validated
`NativeBridgeLoadLibraryExt` RVA `0x38b0`. This avoids MEmu's non-executable
ARM64 shadow library at `/system/lib64/arm64/libnativebridge.so`.

The helper restarts Hay Day, retries the early-load race, waits for startup
integrity checks, and installs the hooks. Records are written to
`/data/local/tmp/hayday-debug/packets.jsonl`. Stream them to Windows:

```bash
"${ADB_PATH:-adb}" -s 127.0.0.1:21503 exec-out \
    tail -n 0 -f /data/local/tmp/hayday-debug/packets.jsonl
```

To clear the existing capture and then follow incoming and outgoing messages
together:

```bash
just listen-messages
# Or without Just:
bash scripts/listen-messages.sh --serial 127.0.0.1:21503
```

Trigger a network reconnect after status becomes `ready` when a fresh complete
login exchange is needed.

To also capture the decrypted runtime `libg.so` before either hook branch
modifies it:

```bash
bash scripts/deploy-memu.sh --dump-libg
```

After the startup-integrity delay, the root helper dumps the main guest mapping
and separate protected mapping immediately before installing hooks. It pulls
both raw spans, saves the corresponding `/proc/<pid>/maps` entries, and overlays
the decrypted descriptor ranges onto the original 1.72.84 ELF. The results are
written under `.hayday/dumps/runtime/`:

```text
libg.runtime-main.bin
libg.runtime-protected.bin
libg.runtime-maps.txt
libg.runtime-decrypted.so
```

Reconstruction is version-locked and verifies the original APK library's
SHA-256 before applying runtime bytes. Differences from the established
1.72.84 runtime hashes are reported as warnings for investigation.

Example record:

```json
{"seq":1,"direction":"out","type":10100,"version":0,"length":76,"redacted":false,"truncated":false,"payload_hex":"..."}
```

Login (`10101`) and LoginOk (`25220`) bodies are redacted by default because
they contain account, token, device, or session data. Their direction, type,
version, and plaintext length are still recorded.

## Diagnostics

Read the current hook status through the configured ADB executable:

```bash
"${ADB_PATH:-adb}" -s 127.0.0.1:21503 shell \
    cat /data/local/tmp/hayday-debug/status.txt
```

The deployment prints `/data/local/tmp/hayday-debug/status.txt` after injection.
The successful status is `ready: Houdini ARM64 hooks installed for Hay Day
1.72.84`. If signatures do not match, verify the installed Hay Day version
before updating any RVA or prologue.

The ADB executable and serial are portable, but native-bridge locations belong
to the emulator system image. The supplied profile is validated against
MEmu/Android 9; the hook signatures remain locked to Hay Day 1.72.84.

## Current validation status

- The Houdini loader, delayed hooks, plaintext JSONL capture, and redaction have
  been exercised end to end on MEmu.
- MEmu validation captured live outbound and inbound records while Hay Day
  remained running after both signature-checked branches were installed.
- The immediate proof captured the supplied `10100` body exactly; stable delayed
  mode remained live and continued capturing later inbound/outbound messages.

## Scope and constraints

- `hd-tool` is application-independent, but the included Hay Day guest hooks
  are locked to 1.72.84/code 2201 through RVAs, prologue signatures, and object
  layouts.
- Native-bridge locations belong to the emulator system image. MEmu's validated
  `NativeBridgeLoadLibraryExt` RVA is `0x38b0`. Recalculate or scan after MEmu
  system-image updates.
- MEmu exposes both an ARM64 shadow bridge and an executable x86_64 bridge.
  `hd-tool` selects executable mappings and rejects `/arm64/` shadows for
  basename searches.
- The ARM64 examples target Android API 28, use conventional RELA relocations,
  and use 16 KiB-compatible ELF alignment so they load under MEmu's Android 9
  Houdini.
- Root ADB is required for `ptrace` and `/proc/<pid>/mem` access.
- Delayed installation intentionally misses the original startup exchange.
  Trigger a network reconnect after the status becomes `ready` to capture a
  fresh login flow.
- Login (`10101`) and LoginOk (`25220`) payload bodies are redacted because they
  may contain account, token, device, or session material.
