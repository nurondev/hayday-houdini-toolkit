# Guest examples

These ARM64 libraries demonstrate payloads loaded through `hd-tool` and
Houdini. They are examples rather than part of the generic injector:

- `guest_probe.cpp` verifies guest constructor execution.
- `guest_hook.cpp` contains the version-specific message capture example.

Configure this architecture independently from `hd-tool`:

```text
cmake -S examples -B build/examples \
  -DCMAKE_TOOLCHAIN_FILE=examples/cmake/aarch64-linux-android28.cmake \
  -DHD_GUEST_LLD=/path/to/ld.lld \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/examples
```
