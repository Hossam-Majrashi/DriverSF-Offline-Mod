#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")"
# Final artifacts only; compiler objects/PDB/import libraries live in a temp dir.
out="${DSF_BUILD_OUTPUT:-$PWD/build}"
mkdir -p build "$out"
tmp="$(mktemp -d "${TMPDIR:-/tmp}/driver-sf-build-XXXXXX")"
trap 'rm -rf -- "$tmp"; rm -f -- build/nitro_script.inc build/world_script.inc' EXIT
python3 tools/embed_nitro_script.py
# Tracked headers already contain the current font/Arabic strings. Regenerate
# after editing ui/ar.json (requires GNU FriBidi), not on every ordinary build.
if [[ "${DSF_REGENERATE_UI:-0}" == 1 ]]; then python3 tools/embed_ui.py; fi
cc=(zig cc -target x86-windows-gnu -O2 -DNDEBUG)
for src in buffer hook trampoline hde/hde32; do
  "${cc[@]}" -Ivendor/minhook/include -Ivendor/minhook/src -c "vendor/minhook/src/$src.c" -o "$tmp/${src//\//_}.o"
done
"${cc[@]}" -c src/nitro_bridge.S -o "$tmp/nitro_bridge.o"
"${cc[@]}" -c src/speed_bridge.S -o "$tmp/speed_bridge.o"
zig c++ -target x86-windows-gnu -std=c++17 -O2 -DNDEBUG -shared \
  -Ivendor/imgui -Ivendor/minhook/include \
  src/mod.cpp vendor/imgui/imgui.cpp vendor/imgui/imgui_draw.cpp \
  vendor/imgui/imgui_tables.cpp vendor/imgui/imgui_widgets.cpp \
  vendor/imgui/backends/imgui_impl_win32.cpp vendor/imgui/backends/imgui_impl_dx9.cpp \
  "$tmp/buffer.o" "$tmp/hook.o" "$tmp/trampoline.o" "$tmp/hde_hde32.o" "$tmp/nitro_bridge.o" "$tmp/speed_bridge.o" \
  src/dinput8.def -ld3d9 -lxinput1_3 -ldwmapi -lgdi32 -limm32 -ladvapi32 -luuid -o "$tmp/dinput8.dll"
cp -- "$tmp/dinput8.dll" "$out/dinput8.dll"
(cd -- "$out"; sha256sum dinput8.dll > SHA256SUMS)
file "$out/dinput8.dll"
