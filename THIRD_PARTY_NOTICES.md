# Third-party notices

## العربية

يتضمن المشروع نسخًا مصدرية محلية من التبعيات اللازمة للبناء فقط. يجب إبقاء إشعارات حقوق النشر والتراخيص معها عند إعادة التوزيع:

- **Dear ImGui 1.92.5** — ترخيص MIT، في [`vendor/imgui/LICENSE.txt`](vendor/imgui/LICENSE.txt). المصدر الأصلي: https://github.com/ocornut/imgui، المرجع `6d910d5487d11ca567b61c7824b0c78c569d62f0`.
- **MinHook / HDE** — شروط BSD المضمنة في [`vendor/minhook/LICENSE.txt`](vendor/minhook/LICENSE.txt). المصدر الأصلي: https://github.com/TsudaKageyu/minhook، المرجع `c3fcafdc10146beb5919319d0683e44e3c30d537`.
- **DejaVu Sans** — الخط غير معدّل، وترخيصه في [`assets/DejaVu-LICENSE.txt`](assets/DejaVu-LICENSE.txt). بايتاته مضمنة في DLL لدعم العربية دون تثبيت خط داخل Wine.
- **GNU FriBidi** — أداة بناء خارجية لتشكيل النصوص الثابتة وترتيبها، غير مضمّنة أو مربوطة بمكتبتها داخل DLL. المصدر: https://github.com/fribidi/fribidi.

- **Lua 5.1** — مترجم وأداة اختبار خارجيان بترخيص MIT؛ غير موزّعين هنا، ولا تضمّن الحزمة آلة Lua افتراضية إضافية. الـbytecode المرفق ناتج سكربتات المود المؤلَّفة فقط، وليس سكربتات اللعبة. المصدر: https://www.lua.org/versions.html.

اللعبة وعلاماتها وملفاتها ملك لأصحابها، وليست جزءًا من هذه الحزمة. هذه الإشعارات لا تختار ترخيصًا جديدًا لكود المود المؤلف.

## English

Only the dependency sources needed for this build are vendored. Keep their copyright/license notices when redistributing:

- **Dear ImGui 1.92.5** — MIT; [`vendor/imgui/LICENSE.txt`](vendor/imgui/LICENSE.txt). Upstream https://github.com/ocornut/imgui, revision `6d910d5487d11ca567b61c7824b0c78c569d62f0`.
- **MinHook / HDE** — included BSD-style terms; [`vendor/minhook/LICENSE.txt`](vendor/minhook/LICENSE.txt). Upstream https://github.com/TsudaKageyu/minhook, revision `c3fcafdc10146beb5919319d0683e44e3c30d537`.
- **DejaVu Sans** — unmodified font; [`assets/DejaVu-LICENSE.txt`](assets/DejaVu-LICENSE.txt). Font bytes are embedded for Arabic rendering without prefix-specific font installation.
- **GNU FriBidi** — external build-only shaping/bidi tool, neither bundled nor linked into the DLL. Upstream https://github.com/fribidi/fribidi.

- **Lua 5.1** — external MIT-licensed compiler/test tool, not redistributed here; no additional Lua VM is embedded. Included bytecode is compiled from authored mod scripts only, not game scripts. Upstream https://www.lua.org/versions.html.

The game, its trademarks and assets belong to their respective owners and are not included. These notices do not select a new license for the mod's authored code.
