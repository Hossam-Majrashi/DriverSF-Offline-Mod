#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
game="$(dirname -- "$here")"
expected=c79987b0548f7130337c3f295c8fe269e120ceddad55c1159e6f5cbc6aa26616
if [[ "$(sha256sum "$game/Driver.exe" | cut -d' ' -f1)" != "$expected" ]]; then
  echo 'Unsupported Driver.exe hash; not installing.' >&2; exit 1
fi
[[ -f "$here/build/dinput8.dll" ]] || { echo 'Build mod/build/dinput8.dll first.' >&2; exit 1; }
if pgrep -x Driver.exe >/dev/null; then echo 'Driver.exe is running; no DLL files changed. Close the game first.' >&2; exit 1; fi
backupMode="${DSF_BACKUPS:-0}"
case "$backupMode" in 0|1) ;; *) echo 'DSF_BACKUPS must be 0 or 1.' >&2; exit 1 ;; esac
if [[ -e "$game/dinput8.dll" ]]; then
  if ! cmp -s "$game/dinput8.dll" "$here/build/dinput8.dll"; then
    recorded=''
    [[ ! -f "$here/installed-dll.sha256" ]] || recorded="$(cut -d' ' -f1 < "$here/installed-dll.sha256")"
    current="$(sha256sum "$game/dinput8.dll" | cut -d' ' -f1)"
    if [[ ! "$recorded" =~ ^[a-f0-9]{64}$ || "$current" != "$recorded" ]]; then
      echo 'Unrecognized dinput8.dll conflict; not overwriting.' >&2; exit 1
    fi
    if [[ "$backupMode" == 1 ]]; then
      mkdir -p "$here/backups"
      backup="$(mktemp -d "$here/backups/dll-update-$(date +%Y%m%d-%H%M%S)-XXXXXX")"
      cp -p -- "$game/dinput8.dll" "$backup/dinput8.dll"
      cp -p -- "$here/installed-dll.sha256" "$backup/installed-dll.sha256"
      printf 'Previous mod DLL backed up: %s\n' "$backup"
    fi
    staged="$(mktemp "$game/.dinput8-update-XXXXXX")"
    trap 'rm -f -- "$staged"' EXIT
    cp -- "$here/build/dinput8.dll" "$staged"
    mv -f -- "$staged" "$game/dinput8.dll"
    trap - EXIT
  fi
else
  # There was no original dinput8.dll in this installation. This adds a new file.
  cp -n -- "$here/build/dinput8.dll" "$game/dinput8.dll"
fi
sha256sum "$game/dinput8.dll" > "$here/installed-dll.sha256"
printf 'Installed standalone proxy: %s\n' "$game/dinput8.dll"

if [[ "$(uname -s)" == "Linux" ]]; then
  prefix="${DSF_WINEPREFIX:-$HOME/Games/driver-san-francisco}"
  if [[ ! -f "$prefix/system.reg" || ! -d "$prefix/drive_c" ]]; then
    printf 'Initializing compatibility prefix at: %s\n' "$prefix"
    if command -v python3 >/dev/null && [[ -f "$here/tools/find_proton.py" ]]; then
      runner="$(python3 "$here/tools/find_proton.py" "$game" 2>/dev/null || true)"
      if [[ -n "$runner" ]]; then
        umu="${DSF_UMU:-}"
        if [[ -z "$umu" ]]; then
          umu="$(command -v umu-run || true)"
          [[ -n "$umu" ]] || umu="$HOME/.local/share/lutris/runtime/umu/umu-run"
        fi
        mkdir -p "$prefix"
        if [[ -n "$umu" && -f "$umu" ]]; then
          WINEPREFIX="$prefix" PROTONPATH="$runner" GAMEID=umu-default STORE=ubisoft UMU_RUNTIME_UPDATE=0 \
            python3 "$umu" createprefix >/dev/null 2>&1 || true
        fi
        if [[ ! -f "$prefix/system.reg" || ! -d "$prefix/drive_c" ]] && [[ -f "$runner/proton" ]]; then
          STEAM_COMPAT_DATA_PATH="$prefix" STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/.steam/steam}" \
            "$runner/proton" run wineboot -u >/dev/null 2>&1 || true
          if [[ -d "$prefix/pfx" && ! -f "$prefix/system.reg" ]]; then
            cp -rn "$prefix/pfx/"* "$prefix/" 2>/dev/null || true
          fi
        fi
      fi
    fi
  fi
  if [[ -f "$prefix/system.reg" && -d "$prefix/drive_c" ]]; then
    printf 'Compatibility prefix ready: %s\n' "$prefix"
  fi
  offlineCfg='OnlineUserName Player'$'\n''OnlineID 1'$'\n'
  graphicsCfg='Fullscreen = 1'$'\n''VSync = 1'$'\n''PreciseVSync = 0'$'\n''Width = 1920'$'\n''Height = 1080'$'\n''RenderQuality = 2'$'\n''AntiAliasing = 2'$'\n''RadialBlur = 1'$'\n'
  mkdir -p "$HOME/Documents/Ubisoft/Driver San Francisco"
  [[ -f "$HOME/Documents/Ubisoft/Driver San Francisco/offline.ini" ]] || printf '%s' "$offlineCfg" > "$HOME/Documents/Ubisoft/Driver San Francisco/offline.ini"
  if [[ ! -f "$HOME/Documents/Ubisoft/Driver San Francisco/graphics.ini" ]] || grep -q 'RenderQuality = 0' "$HOME/Documents/Ubisoft/Driver San Francisco/graphics.ini" 2>/dev/null; then
    printf '%s' "$graphicsCfg" > "$HOME/Documents/Ubisoft/Driver San Francisco/graphics.ini"
  fi
  if [[ -d "$prefix/drive_c/users" ]]; then
    for u in "$prefix/drive_c/users/"*; do
      if [[ -d "$u" ]]; then
        for doc in "$u/Documents" "$u/My Documents"; do
          if [[ -d "$doc" ]]; then
            mkdir -p "$doc/Ubisoft/Driver San Francisco"
            [[ -f "$doc/Ubisoft/Driver San Francisco/offline.ini" ]] || printf '%s' "$offlineCfg" > "$doc/Ubisoft/Driver San Francisco/offline.ini"
            if [[ ! -f "$doc/Ubisoft/Driver San Francisco/graphics.ini" ]] || grep -q 'RenderQuality = 0' "$doc/Ubisoft/Driver San Francisco/graphics.ini" 2>/dev/null; then
              printf '%s' "$graphicsCfg" > "$doc/Ubisoft/Driver San Francisco/graphics.ini"
            fi
            if [[ -f "$here/assets/autosave100.dat" ]]; then
              for target in "$doc/Ubisoft/Driver San Francisco/Player/savegames/autosave.dat" "$doc/Ubisoft/Driver San Francisco/savegames/autosave.dat"; do
                if [[ ! -f "$target" ]]; then
                  mkdir -p "$(dirname -- "$target")"
                  cp -n "$here/assets/autosave100.dat" "$target"
                fi
              done
            fi
          fi
        done
      fi
    done
  fi
fi
