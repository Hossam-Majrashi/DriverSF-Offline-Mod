# خط الواجهة

## العربية

خط **DejaVu Sans** غير معدل، مضمن في DLL لعرض العربية دون الاعتماد على خطوط
Wine. الترخيص: `DejaVu-LICENSE.txt`.

SHA-256: `6038a160b491e121c1f12c7bccb4a9c8730296e3adc1086a059404ed84b7451c`.

الأداة `tools/embed_ui.py` تولد `src/ui_font.h` من هذا الملف، وتشكل النصوص
الثابتة في `ui/ar.json` باستخدام GNU FriBidi. لا تُربط مكتبة تشكيل خارجية
داخل DLL؛ لا تعكس الحروف يدويًا عند تعديل الترجمات.

## English

Unmodified **DejaVu Sans** is embedded for Arabic rendering without relying on
Wine-prefix fonts. License: `DejaVu-LICENSE.txt`.

SHA-256: `6038a160b491e121c1f12c7bccb4a9c8730296e3adc1086a059404ed84b7451c`.

`tools/embed_ui.py` generates `src/ui_font.h` from these bytes and shapes/bidi-
orders fixed labels in `ui/ar.json` with GNU FriBidi. No external shaping library
is linked into the DLL. Do not manually reverse Arabic characters.
