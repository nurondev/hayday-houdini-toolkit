#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
    cat <<'EOF'
Usage: deploy-memu.sh [options]

Options:
  --serial SERIAL               ADB serial (default: 127.0.0.1:21503)
  --hook-delay-seconds SECONDS  Delay before branch installation (default: 15)
  --attempts COUNT              Early-load attempts (default: 5)
  --dump-libg                   Collect and reconstruct the runtime libg.so
  --dump-output-dir PATH        Runtime dump output directory
  --original-libg PATH          Original APK libg.so used for reconstruction
  --verbose                     Show deployment progress and device output
  -h, --help                    Show this help
EOF
}

serial="127.0.0.1:21503"
hook_delay_seconds=6
attempts=5
dump_libg=false
dump_output_dir=""
original_libg=""
verbose=false

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial)
            [[ $# -ge 2 ]] || die "--serial requires a value"
            serial="$2"
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
            dump_libg=true
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
        --verbose)
            verbose=true
            shift
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

arguments=(
    --serial "$serial"
    --bridge-library /system/lib64/libnativebridge.so
    --bridge-load-rva 0x38b0
    --hook-delay-seconds "$hook_delay_seconds"
    --attempts "$attempts"
)
if [[ "$dump_libg" == true ]]; then
    arguments+=(--dump-libg)
fi
if [[ -n "$dump_output_dir" ]]; then
    arguments+=(--dump-output-dir "$dump_output_dir")
fi
if [[ -n "$original_libg" ]]; then
    arguments+=(--original-libg "$original_libg")
fi

if [[ "$verbose" == true ]]; then
    "$script_dir/deploy.sh" "${arguments[@]}"
    printf 'successfully injected\n'
    exit 0
fi

if deployment_output="$("$script_dir/deploy.sh" "${arguments[@]}" 2>&1)"; then
    printf 'successfully injected\n'
else
    result=$?
    printf '%s\n' "$deployment_output" >&2
    exit "$result"
fi
