set shell := ["bash", "-euo", "pipefail", "-c"]

memu_serial := "127.0.0.1:21503"
device_dir := "/data/local/tmp/hayday-debug"
default_guest_library := device_dir + "/libhdguest-hook.so"
default_original_libg := ".hayday/unpacked/split_config.arm64_v8a/lib/arm64-v8a/libg.so"
default_dump_output := ".hayday/dumps/runtime"

# List the available toolkit recipes.
default:
    @just --list

# Describe every script and whether it is a host entry point or internal helper.
scripts:
    @printf '%s\n' \
        'scripts/build-probe.sh          Build hd-tool and all guest examples.' \
        'scripts/deploy.sh               Generic host-side deployment entry point.' \
        'scripts/deploy-memu.sh          MEmu deployment profile and defaults.' \
        'scripts/collect-runtime-libg.sh Pull and reconstruct the runtime libg.so.' \
        'scripts/listen-messages.sh      Clear and follow incoming/outgoing records.' \
        'scripts/common.sh               Shared ADB and WSL path helpers; sourced internally.' \
        'scripts/launch-inject.sh        Android-side launcher; pushed and called by deploy.sh.'

# Build the x86_64 hd-tool and ARM64 guest examples.
build:
    bash scripts/build-probe.sh

# Deploy guest_hook using an explicit native-bridge profile.
deploy serial bridge_library bridge_load_rva hook_delay_seconds="12" attempts="5":
    bash scripts/deploy.sh \
        --serial "{{ serial }}" \
        --bridge-library "{{ bridge_library }}" \
        --bridge-load-rva "{{ bridge_load_rva }}" \
        --hook-delay-seconds "{{ hook_delay_seconds }}" \
        --attempts "{{ attempts }}"

# Deploy guest_hook and collect runtime libg using an explicit bridge profile.
deploy-dump serial bridge_library bridge_load_rva output_dir=default_dump_output original_libg=default_original_libg hook_delay_seconds="12" attempts="5":
    bash scripts/deploy.sh \
        --serial "{{ serial }}" \
        --bridge-library "{{ bridge_library }}" \
        --bridge-load-rva "{{ bridge_load_rva }}" \
        --hook-delay-seconds "{{ hook_delay_seconds }}" \
        --attempts "{{ attempts }}" \
        --dump-libg \
        --dump-output-dir "{{ output_dir }}" \
        --original-libg "{{ original_libg }}"

# Deploy guest_hook with the validated MEmu profile.
deploy-memu serial=memu_serial hook_delay_seconds="6" attempts="5":
    bash scripts/deploy-memu.sh \
        --serial "{{ serial }}" \
        --hook-delay-seconds "{{ hook_delay_seconds }}" \
        --attempts "{{ attempts }}"

# Deploy on MEmu and collect the decrypted runtime libg.so.
deploy-memu-dump serial=memu_serial output_dir=default_dump_output original_libg=default_original_libg hook_delay_seconds="15" attempts="5":
    bash scripts/deploy-memu.sh \
        --serial "{{ serial }}" \
        --hook-delay-seconds "{{ hook_delay_seconds }}" \
        --attempts "{{ attempts }}" \
        --dump-libg \
        --dump-output-dir "{{ output_dir }}" \
        --original-libg "{{ original_libg }}"

# Pull existing runtime dumps and reconstruct libg.so without redeploying.
collect-runtime-libg serial=memu_serial output_dir=default_dump_output original_libg=default_original_libg:
    bash scripts/collect-runtime-libg.sh \
        --serial "{{ serial }}" \
        --output-dir "{{ output_dir }}" \
        --original-libg "{{ original_libg }}"

# Invoke the already-pushed Android launcher directly; deploy first to stage its files.
launch-inject serial=memu_serial guest_library=default_guest_library bridge_load_rva="0x38b0" hook_delay_seconds="15" dump_libg="0" bridge_library="/system/lib64/libnativebridge.so":
    bash -c 'source scripts/common.sh; resolve_adb; adb_run "$1" shell "$2 $3 $4 $5 $6 $7"' \
        _ "{{ serial }}" "{{ device_dir }}/launch-inject.sh" "{{ guest_library }}" \
        "{{ bridge_load_rva }}" "{{ hook_delay_seconds }}" "{{ dump_libg }}" \
        "{{ bridge_library }}"

# List devices visible through the configured adb or adb.exe.
devices:
    bash -c 'source scripts/common.sh; resolve_adb; "$adb_path" devices -l'

# Print the current guest_hook status from the emulator.
status serial=memu_serial:
    bash -c 'source scripts/common.sh; resolve_adb; adb_run "$1" shell cat "{{ device_dir }}/status.txt"' \
        _ "{{ serial }}"

# Clear existing records, then follow incoming and outgoing messages.
listen-messages serial=memu_serial:
    bash scripts/listen-messages.sh --serial "{{ serial }}"
