#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# shellcheck source=common.sh
source "$script_dir/common.sh"

usage() {
    cat <<'EOF'
Usage: listen-messages.sh [options]

Clear existing records, then follow incoming and outgoing messages until
interrupted.

Options:
  --serial SERIAL  ADB serial (default: 127.0.0.1:21503)
  -h, --help       Show this help
EOF
}

serial="127.0.0.1:21503"
capture_path="/data/local/tmp/hayday-debug/packets.jsonl"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --serial)
            [[ $# -ge 2 ]] || die "--serial requires a value"
            serial="$2"
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

resolve_adb
if ! adb_run "$serial" shell test -f "$capture_path"; then
    die "packet capture does not exist; deploy guest_hook first"
fi

adb_run "$serial" shell ": > $capture_path"
adb_run "$serial" exec-out tail -n +1 -f "$capture_path"
