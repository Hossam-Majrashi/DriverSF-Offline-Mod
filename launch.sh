#!/usr/bin/env bash
set -euo pipefail
here="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
game="$(dirname -- "$here")"
command -v python3 >/dev/null || { echo 'Python 3 is required.' >&2; exit 1; }
# Prefix path: existing prefix or auto-initialized on first run.
export WINEPREFIX="${DSF_WINEPREFIX:-$HOME/Games/driver-san-francisco}"
if [[ -n "${DSF_PROTONPATH:-}" ]]; then
  export PROTONPATH="$DSF_PROTONPATH"
else
  export PROTONPATH
  PROTONPATH="$(python3 "$here/tools/find_proton.py" "$game")"
fi
umu="${DSF_UMU:-}"
if [[ -z "$umu" ]]; then
  umu="$(command -v umu-run || true)"
  [[ -n "$umu" ]] || umu="$HOME/.local/share/lutris/runtime/umu/umu-run"
fi
[[ -x "$PROTONPATH/files/bin/wine" && -f "$PROTONPATH/proton" ]] || { echo "Missing selected Proton runner: $PROTONPATH" >&2; exit 1; }
if [[ ! -f "$WINEPREFIX/system.reg" || ! -d "$WINEPREFIX/drive_c" ]]; then
  echo "Initializing prefix: $WINEPREFIX" >&2
  mkdir -p "$WINEPREFIX"
  if [[ -n "$umu" && -f "$umu" ]]; then
    WINEPREFIX="$WINEPREFIX" PROTONPATH="$PROTONPATH" GAMEID=umu-default STORE=ubisoft UMU_RUNTIME_UPDATE=0 \
      python3 "$umu" createprefix >/dev/null 2>&1 || true
  fi
  if [[ ! -f "$WINEPREFIX/system.reg" || ! -d "$WINEPREFIX/drive_c" ]] && [[ -f "$PROTONPATH/proton" ]]; then
    STEAM_COMPAT_DATA_PATH="$WINEPREFIX" STEAM_COMPAT_CLIENT_INSTALL_PATH="${STEAM_COMPAT_CLIENT_INSTALL_PATH:-$HOME/.steam/steam}" \
      "$PROTONPATH/proton" run wineboot -u >/dev/null 2>&1 || true
    if [[ -d "$WINEPREFIX/pfx" && ! -f "$WINEPREFIX/system.reg" ]]; then
      cp -rn "$WINEPREFIX/pfx/"* "$WINEPREFIX/" 2>/dev/null || true
    fi
  fi
fi
[[ -f "$WINEPREFIX/system.reg" && -d "$WINEPREFIX/drive_c" ]] || { echo "Missing existing prefix: $WINEPREFIX" >&2; exit 1; }
[[ -f "$umu" ]] || { echo "Missing UMU runtime: $umu (set DSF_UMU)." >&2; exit 1; }
# Ensure offline profile and optimal graphics settings exist so the game never prompts for online login or resets to low quality
offlineCfg='OnlineUserName Player'$'\n''OnlineID 1'$'\n'
graphicsCfg='Fullscreen = 1'$'\n''VSync = 1'$'\n''PreciseVSync = 0'$'\n''Width = 1920'$'\n''Height = 1080'$'\n''RenderQuality = 2'$'\n''AntiAliasing = 2'$'\n''RadialBlur = 1'$'\n'
mkdir -p "$HOME/Documents/Ubisoft/Driver San Francisco"
[[ -f "$HOME/Documents/Ubisoft/Driver San Francisco/offline.ini" ]] || printf '%s' "$offlineCfg" > "$HOME/Documents/Ubisoft/Driver San Francisco/offline.ini"
if [[ ! -f "$HOME/Documents/Ubisoft/Driver San Francisco/graphics.ini" ]] || grep -q 'RenderQuality = 0' "$HOME/Documents/Ubisoft/Driver San Francisco/graphics.ini" 2>/dev/null; then
  printf '%s' "$graphicsCfg" > "$HOME/Documents/Ubisoft/Driver San Francisco/graphics.ini"
fi
if [[ -d "$WINEPREFIX/drive_c/users" ]]; then
  for u in "$WINEPREFIX/drive_c/users/"*; do
    if [[ -d "$u" ]]; then
      for doc in "$u/Documents" "$u/My Documents"; do
        if [[ -d "$doc" ]]; then
          mkdir -p "$doc/Ubisoft/Driver San Francisco"
          [[ -f "$doc/Ubisoft/Driver San Francisco/offline.ini" ]] || printf '%s' "$offlineCfg" > "$doc/Ubisoft/Driver San Francisco/offline.ini"
          if [[ ! -f "$doc/Ubisoft/Driver San Francisco/graphics.ini" ]] || grep -q 'RenderQuality = 0' "$doc/Ubisoft/Driver San Francisco/graphics.ini" 2>/dev/null; then
            printf '%s' "$graphicsCfg" > "$doc/Ubisoft/Driver San Francisco/graphics.ini"
          fi
        fi
      done
    fi
  done
fi
# Prefer mapped SDL/XInput for this DS4 only; no global remap/registry changes.
ds4Mode="${DSF_DS4_XINPUT:-1}"
case "$ds4Mode" in 0|1) ;; *) echo 'DSF_DS4_XINPUT must be 0 or 1.' >&2; exit 1 ;; esac
if [[ "$ds4Mode" == 1 ]]; then
  hidraw="${PROTON_DISABLE_HIDRAW:-}"
  if [[ "$hidraw" != 1 && ",${hidraw,,}," != *,0x054c/0x05c4,* ]]; then
    export PROTON_DISABLE_HIDRAW="${hidraw:+$hidraw,}0x054c/0x05c4"
  fi
fi
if [[ "${1:-}" == --check ]]; then
  printf 'Game: %s\nPrefix: %s\nRunner: %s\nUMU: %s\n' "$game/Driver.exe" "$WINEPREFIX" "$PROTONPATH" "$umu"
  [[ ! -f "$PROTONPATH/version" ]] || printf 'Proton build: %s\n' "$(<"$PROTONPATH/version")"
  "$PROTONPATH/files/bin/wine" --version
  printf 'DS4 SDL/XInput compatibility requested: %s\nPROTON_DISABLE_HIDRAW: %s\n' "$ds4Mode" "${PROTON_DISABLE_HIDRAW:-<unset>}"
  sha256sum "$game/Driver.exe"
  exit 0
fi
if pgrep -x Driver.exe >/dev/null; then echo 'Driver.exe is already running; close it first.' >&2; exit 1; fi
backupMode="${DSF_BACKUPS:-0}"
case "$backupMode" in 0|1) ;; *) echo 'DSF_BACKUPS must be 0 or 1.' >&2; exit 1 ;; esac
mode=mod
if [[ "${1:-}" == --vanilla ]]; then mode=vanilla; shift; fi
if [[ "$mode" == mod ]]; then "$here/install.sh"; override='dinput8=n,b'; else override='dinput8=b'; fi
if [[ "$backupMode" == 1 ]]; then python3 "$here/tools/backup_saves.py" "$WINEPREFIX"; fi
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}$override"
export GAMEID=umu-default STORE=ubisoft UMU_RUNTIME_UPDATE=0
cd -- "$game"
mkdir -p "$here/logs"
log="$here/logs/launch-$(date +%Y%m%d-%H%M%S).log"
printf 'Launching %s (-offline) with %s. Log: %s\n' "$mode" "$PROTONPATH" "$log"
printf 'Runner: %s\nPrefix: %s\nDS4 SDL/XInput compatibility requested: %s\nPROTON_DISABLE_HIDRAW: %s\n' "$PROTONPATH" "$WINEPREFIX" "$ds4Mode" "${PROTON_DISABLE_HIDRAW:-<unset>}" >"$log"
[[ ! -f "$PROTONPATH/version" ]] || printf 'Proton build: %s\n' "$(<"$PROTONPATH/version")" >>"$log"
# UMU is a Python entry point. This does not bypass game ownership requirements.
exec python3 "$umu" "$game/Driver.exe" -offline "$@" >>"$log" 2>&1
