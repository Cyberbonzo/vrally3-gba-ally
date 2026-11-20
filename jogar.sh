#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")" && pwd)"
engine="$root/tools/gba-recomp-cli-0.4-x86_64-unknown-linux-gnu/recomp"
export XDG_CONFIG_HOME="$root/config"
export XDG_CACHE_HOME="$root/cache"
export PIPEWIRE_LATENCY=256/48000
export PIPEWIRE_ALSA='{ alsa.period-bytes=2048 alsa.buffer-bytes=8192 }'
unset LD_PRELOAD
cd "$root"

rom="$root/rom/vrally3-us.gba"
rom_hash="$(sha256sum "$rom" | cut -d' ' -f1)"
expected_hash=5818fa74fdcacf39b53a1acea99432c4362979dd656c8153fd9861094aca51f7
native="$root/native/vrally3-us.so"
unset VRALLY3_HD_RENDERER
if [[ "$rom_hash" == "$expected_hash" && "${VRALLY3_ORIGINAL_VIDEO:-0}" != 1 &&
      -x "$root/tools/recomp-v3d" && -r "$root/native/v3d/original.so" &&
      -r "$root/native/v3d/additional.so" && -r "$root/native/v3d/vrally3-us.so" ]]; then
    engine="$root/tools/recomp-v3d"
    native="$root/native/v3d/vrally3-us.so"
    export VRALLY3_HD_RENDERER="$root/native/v3d/original.so"
fi
if [[ -f "$native" && "$rom_hash" == "$expected_hash" ]]; then
    labels="$("$engine" labels show "$rom" 2>/dev/null)"
    suffix="$(sed -n 's/^cache key suffix: //p' <<<"$labels")"
    if [[ -z "$suffix" || "$suffix" =~ ^-l[0-9a-f]{8}$ ]]; then
        cache="$XDG_CACHE_HOME/gba-recomp/t5"
        key="$rom_hash${suffix}"
        mkdir -p "$cache"
        if [[ "$(readlink "$cache/$key.so" || true)" != "$native" ]]; then
            linkdir="$(mktemp -d "$cache/vrally3-link-XXXXXX")"
            ln -s "$native" "$linkdir/$key.so"
            mv -Tf "$linkdir/$key.so" "$cache/$key.so"
            rmdir "$linkdir"
        fi
    fi
fi

options=(--no-bios --status)
if [[ "${VRALLY3_DIAGNOSTIC:-0}" == 1 ]]; then
    export RECOMP_TRACE_FALLBACK=1
    export RECOMP_RECORD_INPUT="$root/logs/sessao.inputs"
    options+=(--stats --record-labels)
fi

"$engine" play "$rom" "${options[@]}" >"$root/logs/jogo.log" 2>&1 &
game_pid=$!
input_pid=
if [[ -n "${DISPLAY:-}" ]]; then
    python3 "$root/project/gatilhos.py" "$game_pid" >"$root/logs/gatilhos.log" 2>&1 &
    input_pid=$!
fi
cleanup() {
    if [[ -n "$input_pid" ]]; then
        kill -TERM "$input_pid" 2>/dev/null || true
        wait "$input_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT
trap 'kill -TERM "$game_pid" 2>/dev/null || true' INT TERM

# Use the playerâ€™s own fullscreen toggle once its X11 window gets focus.
if [[ -n "${DISPLAY:-}" ]] && command -v xdotool >/dev/null; then
    for ((attempt = 0; attempt < 150; attempt++)); do
        kill -0 "$game_pid" 2>/dev/null || break
        win="$(xdotool getwindowfocus 2>/dev/null || true)"
        title="$(xdotool getwindowname "$win" 2>/dev/null || true)"
        if [[ "$title" == vrally3-us* && "$(xdotool getwindowpid "$win" 2>/dev/null)" == "$game_pid" ]]; then
            sleep 0.7
            if [[ "$(xdotool getwindowfocus 2>/dev/null)" != "$win" ]]; then
                continue
            fi
            read -r screen_width screen_height < <(xdotool getdisplaygeometry)
            read -r win_width win_height < <(xdotool getwindowgeometry --shell "$win" | awk -F= '/^(WIDTH|HEIGHT)=/ {print $2}' | paste -sd ' ')
            if [[ "$win_width" != "$screen_width" || "$win_height" != "$screen_height" ]]; then
                xdotool keydown F11 || true
                sleep 0.07
                xdotool keyup F11 || true
            fi
            break
        fi
        sleep 0.1
    done
fi
wait "$game_pid"

