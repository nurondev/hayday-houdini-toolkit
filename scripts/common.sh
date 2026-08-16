#!/usr/bin/env bash

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

resolve_adb() {
    local candidate="${ADB_PATH:-}"

    if [[ -z "$candidate" ]]; then
        candidate="$(command -v adb 2>/dev/null || true)"
    fi
    if [[ -z "$candidate" ]]; then
        candidate="$(command -v adb.exe 2>/dev/null || true)"
    fi
    [[ -n "$candidate" ]] || die "ADB was not found; set ADB_PATH or add adb to PATH"

    if [[ "$candidate" =~ ^[[:alpha:]]:\\ ]] && command -v wslpath >/dev/null; then
        candidate="$(wslpath -u "$candidate")"
    elif [[ "$candidate" != */* ]]; then
        candidate="$(command -v "$candidate" 2>/dev/null || true)"
    fi

    [[ -n "$candidate" && -f "$candidate" && -x "$candidate" ]] ||
        die "ADB_PATH does not point to an executable file: ${ADB_PATH:-$candidate}"

    adb_path="$candidate"
    if [[ "${adb_path,,}" == *.exe ]]; then
        adb_is_windows=true
        command -v wslpath >/dev/null ||
            die "Windows adb.exe requires wslpath when called from Bash"
    else
        adb_is_windows=false
    fi
}

adb_host_path() {
    local path="$1"

    if [[ "$adb_is_windows" == true ]]; then
        wslpath -w "$(realpath -m "$path")"
    else
        printf '%s\n' "$path"
    fi
}

adb_run() {
    local serial="$1"
    shift
    "$adb_path" -s "$serial" "$@"
}
