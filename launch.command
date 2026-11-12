#!/bin/bash
set -euo pipefail
here="$(cd -- "$(dirname -- "$0")" && pwd)"
game="$(dirname -- "$here")"
[[ "$(uname -s)" == Darwin ]] || { echo 'This launcher requires macOS.' >&2; exit 1; }
if pgrep -x Driver.exe >/dev/null; then echo 'Close Driver.exe first.' >&2; exit 1; fi
backupMode="${DSF_BACKUPS:-0}"
case "$backupMode" in 0|1) ;; *) echo 'DSF_BACKUPS must be 0 or 1.' >&2; exit 1 ;; esac
mkdir -p "$here/logs"
# Use an EXISTING CrossOver bottle or an explicitly selected Wine installation.
if [[ -n "${CX_BOTTLE:-}" ]]; then
  wine="${DSF_WINE:-/Applications/CrossOver.app/Contents/SharedSupport/CrossOver/bin/wine}"
  prefix="${WINEPREFIX:-$HOME/Library/Application Support/CrossOver/Bottles/$CX_BOTTLE}"
  runner=("$wine" --bottle "$CX_BOTTLE")
else
  wine="${DSF_WINE:-}"
  prefix="${WINEPREFIX:-}"
  [[ -n "$wine" && -n "$prefix" ]] || { echo 'Select an existing bottle: CX_BOTTLE=name, or set DSF_WINE=/path/to/wine and WINEPREFIX=/existing/prefix. Nothing has been created.' >&2; exit 1; }
  runner=("$wine")
fi
[[ -x "$wine" && -f "$prefix/system.reg" && -d "$prefix/drive_c" ]] || { echo 'Missing compatibility runner or existing prefix.' >&2; exit 1; }
command -v python3 >/dev/null || { echo 'python3 is required for verified save backups.' >&2; exit 1; }
expected=c79987b0548f7130337c3f295c8fe269e120ceddad55c1159e6f5cbc6aa26616
[[ "$(shasum -a 256 "$game/Driver.exe" | cut -d' ' -f1)" == "$expected" ]] || { echo 'Unsupported Driver.exe hash.' >&2; exit 1; }
[[ -f "$here/build/dinput8.dll" ]] || { echo 'Missing shared Windows x86 mod DLL.' >&2; exit 1; }
if [[ -e "$game/dinput8.dll" ]]; then
  cmp -s "$game/dinput8.dll" "$here/build/dinput8.dll" || { echo 'dinput8.dll conflict; not overwriting.' >&2; exit 1; }
else
  cp -n "$here/build/dinput8.dll" "$game/dinput8.dll"
fi
if [[ "$backupMode" == 1 ]]; then python3 "$here/tools/backup_saves.py" "$prefix"; fi
offlineCfg='OnlineUserName Player'$'\n''OnlineID 1'$'\n'
mkdir -p "$HOME/Documents/Ubisoft/Driver San Francisco"
[[ -f "$HOME/Documents/Ubisoft/Driver San Francisco/offline.ini" ]] || printf '%s' "$offlineCfg" > "$HOME/Documents/Ubisoft/Driver San Francisco/offline.ini"
if [[ -d "$prefix/drive_c/users" ]]; then
  for u in "$prefix/drive_c/users/"*; do
    if [[ -d "$u" ]]; then
      for doc in "$u/Documents" "$u/My Documents"; do
        if [[ -d "$doc" ]]; then
          mkdir -p "$doc/Ubisoft/Driver San Francisco"
          [[ -f "$doc/Ubisoft/Driver San Francisco/offline.ini" ]] || printf '%s' "$offlineCfg" > "$doc/Ubisoft/Driver San Francisco/offline.ini"
        fi
      done
    fi
  done
fi
export WINEPREFIX="$prefix"
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}dinput8=n,b"
cd -- "$game"
exec "${runner[@]}" "$game/Driver.exe" -offline "$@"
