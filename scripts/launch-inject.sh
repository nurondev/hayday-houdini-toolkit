#!/system/bin/sh

package="com.supercell.hayday"
component="com.supercell.hayday/.GameApp"
tool_dir="/data/local/tmp/hayday-debug"
guest_so="${1:-$tool_dir/libhdguest-probe.so}"
load_ext_rva="${2:-0x139c0}"
hook_delay_seconds="${3:-6}"
dump_libg="${4:-0}"
bridge_library="${5:-libnativebridge.so}"

am force-stop "$package"
rm -f "$tool_dir/libg-base.txt" "$tool_dir/hook-plan.txt" \
    "$tool_dir/hooks-ready.flag"
am start -n "$component" >/dev/null &

count=0
while [ "$count" -lt 30000 ]; do
    target_pid="$(pidof "$package")"
    if [ -n "$target_pid" ] \
        && grep -q "$bridge_library" "/proc/$target_pid/maps" 2>/dev/null; then
        echo "candidate_pid=$target_pid poll_count=$count"
        "$tool_dir/hd-tool" inject --pid "$target_pid" \
            --library "$guest_so" --bridge-rva "$load_ext_rva" \
            --bridge "$bridge_library"
        result=$?
        echo "injector_exit=$result"
        if [ "$result" -ne 0 ]; then exit "$result"; fi

        base_poll=0
        while [ "$base_poll" -lt 30000 ]; do
            for guest_base in $(awk '$3 == "00000000" && $NF ~ /\/lib\/arm64\/libg.so$/ { split($1, range, "-"); print range[1] }' "/proc/$target_pid/maps" 2>/dev/null); do
                out_signature="$("$tool_dir/hd-tool" read --pid "$target_pid" \
                    --address "$((0x$guest_base + 0x1100A78))" \
                    --length 16 2>/dev/null | tail -n 1)"
                in_signature="$("$tool_dir/hd-tool" read --pid "$target_pid" \
                    --address "$((0x$guest_base + 0x0ACD804))" \
                    --length 16 2>/dev/null | tail -n 1)"
                if [ "$out_signature" = "ff4302d1fd7b03a9fb2300f9fa6705a9" ] \
                    && [ "$in_signature" = "ffc304d1fd7b0da9fc6f0ea9fa670fa9" ]; then
                    echo "$guest_base" > "$tool_dir/libg-base.txt"
                    chmod 0644 "$tool_dir/libg-base.txt"
                    echo "libg_guest_base=$guest_base"
                    break 2
                fi
            done
            base_poll=$((base_poll + 1))
        done
        plan_poll=0
        while [ "$plan_poll" -lt 400 ]; do
            if [ -s "$tool_dir/hook-plan.txt" ]; then break; fi
            sleep 0.05
            plan_poll=$((plan_poll + 1))
        done
        if [ ! -s "$tool_dir/hook-plan.txt" ]; then
            echo "guest hook plan was not published" >&2
            exit 1
        fi
        out_target="$(grep '^out_target=' "$tool_dir/hook-plan.txt" | cut -d= -f2)"
        out_proxy="$(grep '^out_proxy=' "$tool_dir/hook-plan.txt" | cut -d= -f2)"
        in_target="$(grep '^in_target=' "$tool_dir/hook-plan.txt" | cut -d= -f2)"
        in_proxy="$(grep '^in_proxy=' "$tool_dir/hook-plan.txt" | cut -d= -f2)"
        echo "waiting ${hook_delay_seconds}s for startup integrity checks"
        sleep "$hook_delay_seconds"
        if [ "$dump_libg" = "1" ]; then
            main_dump="$tool_dir/libg.runtime-main.bin"
            protected_dump="$tool_dir/libg.runtime-protected.bin"
            echo "dumping decrypted libg before hook branches"
            "$tool_dir/hd-tool" dump --pid "$target_pid" \
                --address "0x$guest_base" --output "$main_dump" \
                --length 0x141f000 || exit 1
            "$tool_dir/hd-tool" dump --pid "$target_pid" \
                --address "0x$guest_base" --output "$protected_dump" \
                --length 0x59000 --offset 0x15fc000 || exit 1
            grep '/libg.so' "/proc/$target_pid/maps" \
                > "$tool_dir/libg.runtime-maps.txt" || exit 1
            chmod 0644 "$main_dump" "$protected_dump" \
                "$tool_dir/libg.runtime-maps.txt"
        fi
        "$tool_dir/hd-tool" jump --pid "$target_pid" \
            --address "0x$out_target" --target "0x$out_proxy" \
            --expected ff4302d1fd7b03a9fb2300f9fa6705a9 || exit 1
        "$tool_dir/hd-tool" jump --pid "$target_pid" \
            --address "0x$in_target" --target "0x$in_proxy" \
            --expected ffc304d1fd7b0da9fc6f0ea9fa670fa9 || exit 1
        echo ready > "$tool_dir/hooks-ready.flag"
        chmod 0644 "$tool_dir/hooks-ready.flag" "$tool_dir/hook-plan.txt"
        sleep 1
        [ -e "$tool_dir/status.txt" ] && chmod 0644 "$tool_dir/status.txt"
        [ -e "$tool_dir/packets.jsonl" ] && chmod 0666 "$tool_dir/packets.jsonl"
        exit "$result"
    fi
    count=$((count + 1))
done

echo "native bridge did not appear before the poll limit" >&2
exit 1
