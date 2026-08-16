#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"
output_dir="$project_dir/out"
build_dir="$project_dir/build"
linker="${LLD_PATH:-$(command -v ld.lld || true)}"

if [[ -z "$linker" || ! -x "$linker" ]]; then
    echo "LLD was not found. Install lld or set LLD_PATH to its executable." >&2
    exit 1
fi

cmake -S "$project_dir/hd-tool" -B "$build_dir/hd-tool" \
    -DCMAKE_BUILD_TYPE=Release \
    -DHD_TOOL_OUTPUT_DIRECTORY="$output_dir"
cmake --build "$build_dir/hd-tool" --parallel

cmake -S "$project_dir/examples" -B "$build_dir/examples" \
    -DCMAKE_TOOLCHAIN_FILE="$project_dir/examples/cmake/aarch64-linux-android28.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DHD_GUEST_LLD="$linker" \
    -DHD_GUEST_OUTPUT_DIRECTORY="$output_dir"
cmake --build "$build_dir/examples" --parallel

file "$output_dir/hd-tool" \
    "$output_dir/libhdguest-probe.so" \
    "$output_dir/libhdguest-hook.so"
readelf -d "$output_dir/libhdguest-probe.so"
readelf -d "$output_dir/libhdguest-hook.so"
