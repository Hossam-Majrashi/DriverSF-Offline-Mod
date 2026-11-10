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
