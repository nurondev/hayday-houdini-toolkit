#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"

# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
    cat <<'EOF'
Usage: deploy.sh --serial SERIAL --bridge-library PATH --bridge-load-rva RVA [options]

Options:
  --hook-delay-seconds SECONDS  Delay before branch installation (default: 12)
  --attempts COUNT              Early-load attempts (default: 5)
  --dump-libg                   Collect and reconstruct the runtime libg.so
  --dump-output-dir PATH        Runtime dump output directory
  --original-libg PATH          Original APK libg.so used for reconstruction
  -h, --help                    Show this help
EOF
}

serial=""
bridge_library=""
bridge_load_rva=""
hook_delay_seconds=6
attempts=5
dump_libg=0
dump_output_dir=""
original_libg=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial)
            [[ $# -ge 2 ]] || die "--serial requires a value"
            serial="$2"
            shift 2
            ;;
        --bridge-library)
            [[ $# -ge 2 ]] || die "--bridge-library requires a value"
            bridge_library="$2"
            shift 2
            ;;
        --bridge-load-rva)
            [[ $# -ge 2 ]] || die "--bridge-load-rva requires a value"
            bridge_load_rva="$2"
            shift 2
            ;;
        --hook-delay-seconds)
            [[ $# -ge 2 ]] || die "--hook-delay-seconds requires a value"
            hook_delay_seconds="$2"
            shift 2
            ;;
        --attempts)
            [[ $# -ge 2 ]] || die "--attempts requires a value"
            attempts="$2"
            shift 2
            ;;
        --dump-libg)
            dump_libg=1
            shift
            ;;
        --dump-output-dir)
            [[ $# -ge 2 ]] || die "--dump-output-dir requires a value"
            dump_output_dir="$2"
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

[[ -n "$serial" ]] || die "--serial is required"
[[ "$bridge_library" =~ ^[[:alnum:]_./-]+$ ]] ||
    die "--bridge-library is required and contains invalid characters"
[[ "$bridge_load_rva" =~ ^(0[xX][[:xdigit:]]+|[[:digit:]]+)$ ]] ||
    die "--bridge-load-rva must be a decimal or hexadecimal integer"
[[ "$hook_delay_seconds" =~ ^[[:digit:]]+$ ]] ||
    die "--hook-delay-seconds must be a non-negative integer"
[[ "$attempts" =~ ^[1-9][[:digit:]]*$ ]] ||
    die "--attempts must be a positive integer"

output_dir="$project_dir/out"
guest_library="$output_dir/libhdguest-hook.so"
device_dir="/data/local/tmp/hayday-debug"

for required in "$output_dir/hd-tool" "$guest_library" \
    "$script_dir/launch-inject.sh"; do
    [[ -f "$required" ]] || die "missing build artifact: $required"
done

resolve_adb
adb_run "$serial" root
adb_run "$serial" shell \
    "mkdir -p $device_dir; chmod 0777 $device_dir"
adb_run "$serial" push "$(adb_host_path "$output_dir/hd-tool")" \
    "$device_dir/hd-tool" >/dev/null
adb_run "$serial" push "$(adb_host_path "$guest_library")" \
    "$device_dir/libhdguest-hook.so" >/dev/null
adb_run "$serial" push "$(adb_host_path "$script_dir/launch-inject.sh")" \
    "$device_dir/launch-inject.sh" >/dev/null
adb_run "$serial" shell \
    "chmod 0755 $device_dir/hd-tool $device_dir/launch-inject.sh; chmod 0644 $device_dir/libhdguest-hook.so; rm -f $device_dir/status.txt $device_dir/packets.jsonl $device_dir/libg.runtime-main.bin $device_dir/libg.runtime-protected.bin $device_dir/libg.runtime-maps.txt"
printf 'files are ready\n'

installed=false
for ((attempt = 1; attempt <= attempts; ++attempt)); do
    printf 'Early-load attempt %d/%d\n' "$attempt" "$attempts"
    if adb_run "$serial" shell \
        "$device_dir/launch-inject.sh $device_dir/libhdguest-hook.so $bridge_load_rva $hook_delay_seconds $dump_libg $bridge_library"; then
        installed=true
        break
    fi
done
[[ "$installed" == true ]] ||
    die "could not win the early ptrace race after $attempts attempts"

if ((dump_libg)); then
    collector_arguments=(--serial "$serial")
    if [[ -n "$dump_output_dir" ]]; then
        collector_arguments+=(--output-dir "$dump_output_dir")
    fi
    if [[ -n "$original_libg" ]]; then
        collector_arguments+=(--original-libg "$original_libg")
    fi
    "$script_dir/collect-runtime-libg.sh" "${collector_arguments[@]}"
fi

adb_run "$serial" shell \
    "cat $device_dir/status.txt; wc -l -c $device_dir/packets.jsonl"
