#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"

# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
    cat <<'EOF'
Usage: collect-runtime-libg.sh [options]

Options:
  --serial SERIAL       ADB serial (default: emulator-5554)
  --output-dir PATH     Output directory (default: .hayday/dumps/runtime)
  --original-libg PATH  Original APK libg.so used for reconstruction
  -h, --help            Show this help
EOF
}

serial="emulator-5554"
output_dir="$project_dir/.hayday/dumps/runtime"
original_libg="$project_dir/.hayday/unpacked/split_config.arm64_v8a/lib/arm64-v8a/libg.so"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial)
            [[ $# -ge 2 ]] || die "--serial requires a value"
            serial="$2"
            shift 2
            ;;
        --output-dir)
            [[ $# -ge 2 ]] || die "--output-dir requires a value"
            output_dir="$2"
            shift 2
            ;;
        --original-libg)
            [[ $# -ge 2 ]] || die "--original-libg requires a value"
            original_libg="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

expected_original_hash="7ec0016219a357516b9d056d30b372d05234de218aea0e0f504be8abb51922bd"
[[ -f "$original_libg" ]] || die "original libg.so not found: $original_libg"
original_hash="$(sha256sum "$original_libg" | awk '{print $1}')"
[[ "$original_hash" == "$expected_original_hash" ]] ||
    die "original libg.so does not match Hay Day 1.72.84: $original_hash"

mkdir -p "$output_dir"
main_dump="$output_dir/libg.runtime-main.bin"
protected_dump="$output_dir/libg.runtime-protected.bin"
maps_dump="$output_dir/libg.runtime-maps.txt"
reconstructed="$output_dir/libg.runtime-decrypted.so"

resolve_adb
adb_run "$serial" pull /data/local/tmp/hayday-debug/libg.runtime-main.bin \
    "$(adb_host_path "$main_dump")"
adb_run "$serial" pull /data/local/tmp/hayday-debug/libg.runtime-protected.bin \
    "$(adb_host_path "$protected_dump")"
adb_run "$serial" pull /data/local/tmp/hayday-debug/libg.runtime-maps.txt \
    "$(adb_host_path "$maps_dump")"

[[ "$(stat -c %s "$main_dump")" -eq $((0x141f000)) ]] ||
    die "unexpected main dump size"
[[ "$(stat -c %s "$protected_dump")" -eq $((0x59000)) ]] ||
    die "unexpected protected dump size"

cp "$original_libg" "$reconstructed"
dd if="$main_dump" of="$reconstructed" bs=1M count=$((0x141e010)) \
    iflag=count_bytes conv=notrunc status=none
dd if="$protected_dump" of="$reconstructed" bs=1M seek=$((0x15f4000)) \
    count=$((0x46c8)) iflag=count_bytes oflag=seek_bytes conv=notrunc \
    status=none

main_hash="$(sha256sum "$main_dump" | awk '{print $1}')"
protected_hash="$(sha256sum "$protected_dump" | awk '{print $1}')"
reconstructed_hash="$(sha256sum "$reconstructed" | awk '{print $1}')"
printf 'main_sha256=%s\n' "$main_hash"
printf 'protected_sha256=%s\n' "$protected_hash"
printf 'reconstructed_sha256=%s\n' "$reconstructed_hash"

if [[ "$main_hash" != "d9868264a47021da2286d1016e77b80a966b78eeee67603c193040367ad4a58d" ]]; then
    printf 'warning: main runtime dump differs from the validated 1.72.84 capture\n' >&2
fi
if [[ "$protected_hash" != "dd4b3681f1e3751a96756ef15e274d44657c01c302377dc066b2a143ea7b41bb" ]]; then
    printf 'warning: protected runtime dump differs from the validated 1.72.84 capture\n' >&2
fi
if [[ "$reconstructed_hash" != "061c5639a7126e12e84f87e6cfb9acdce7a1ad79db680686637056ece192403b" ]]; then
    printf 'warning: reconstructed ELF differs from the validated 1.72.84 artifact\n' >&2
fi

stat --printf='%n %s bytes\n' \
    "$main_dump" "$protected_dump" "$maps_dump" "$reconstructed"
