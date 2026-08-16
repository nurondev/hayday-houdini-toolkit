# hd-tool

`hd-tool` is the architecture-native device utility. It runs inside an x86_64
Android emulator and deliberately contains no game-specific RVAs or structure
layouts.

## Source layout

```text
src/
├── main.cpp                    Minimal process entry point
├── app/
│   ├── application.hpp
│   └── application.cpp         CLI parsing and command dispatch
├── injector/
│   ├── injector.hpp
│   └── injector.cpp            ptrace/native-bridge injection
└── memory/
    ├── proc_maps.hpp
    ├── proc_maps.cpp           Process mapping discovery
    ├── process_memory.hpp
    └── process_memory.cpp      Memory I/O, scanning, dumps, and jumps
```

Current scaffolded commands:

```text
hd-tool inject --pid PID --library GUEST_SO \
    (--bridge-rva RVA [--bridge MODULE] | --bridge-address ADDRESS)
hd-tool read --pid PID --address ADDRESS [--length LENGTH]
hd-tool write --pid PID --address ADDRESS --bytes HEX
hd-tool dump --pid PID --address ADDRESS --output FILE --length LENGTH \
    [--offset OFFSET]
hd-tool jump --pid PID --address ADDRESS --target ADDRESS --expected HEX16
hd-tool scan --pid PID --module MODULE --pattern "AA BB ?? DD"
```

Injection currently intercepts the next call to
`NativeBridgeLoadLibraryExt`, substitutes the supplied guest library while
preserving the original flags and namespace, and then replays the original
load. Bridge discovery is intentionally caller-driven in this initial
scaffold. A symbol or pattern resolver can later produce `--bridge-address`
without changing the injector.

Build this app independently from the ARM64 examples:

```text
cmake -S hd-tool -B build/hd-tool -DCMAKE_BUILD_TYPE=Release
cmake --build build/hd-tool
```
