# Driver: San Francisco — Offline Mod v7.14

مود قائمة محلية للعب دون اتصال، من تطوير **Hossam Majrashi**.
[GitHub](https://github.com/Hossam-Majrashi)

**هذا المشروع يوزع المود، وليس اللعبة.** يلزم امتلاك نسخة مثبتة أصلية. لا تتضمن
حزمة النشر `Driver.exe` أو أرشيفات اللعبة أو أصواتها أو صورها أو ملفات الحفظ.
لا يقدم المود تجاوزًا للتفعيل أو متطلبات امتلاك اللعبة.

## استعراض المود / Mod Showcase

<p align="center">
  <img src="assets/videos/gameplay.gif" alt="Driver San Francisco Mod Showcase" width="100%">
</p>

[⬇ تنزيل الفيديو الكامل بجودة عالية (MP4) / Download Full Video](https://github.com/Hossam-Majrashi/DriverSF-Offline-Mod/raw/refs/heads/main/assets/videos/video.mp4)

## لقطات من اللعبة / Gameplay Screenshots

<p align="center">
  <img src="assets/screenshots/1.png" alt="Driver San Francisco — Screenshot 1" width="100%">
  <img src="assets/screenshots/2.png" alt="Driver San Francisco — Screenshot 2" width="100%">
  <img src="assets/screenshots/3.png" alt="Driver San Francisco — Screenshot 3" width="100%">
  <img src="assets/screenshots/4.png" alt="Driver San Francisco — Screenshot 4" width="100%">
  <img src="assets/screenshots/5.png" alt="Driver San Francisco — Screenshot 5" width="100%">
</p>

## المزايا والنطاق

- قائمة عربية افتراضيًا مع اختيار الإنجليزية، وتكبير الواجهة.
- سرعات مستهدفة 100 / 200 / 300 / 400 / 500 MPH، بتعديل تدريجي لمحرك وقير
  مركبة اللاعب؛ ليست فرضًا مباشرًا للسرعة. الوصول للهدف والتحكم غير مضمونين
  لكل سيارة، خصوصًا 400 و500.
- إصلاح المركبة وحماية المحرك والهيكل، وخيارات RAM والنيترو والمال وWP
  وألوان المركبة الأصلية المدعومة.
- خيارات المرور والشرطة ومنع ركاب سيارة اللاعب وإشارات الانعطاف، ضمن نطاق
  التجول دون اتصال. منع الركاب يبدأ مفعّلًا؛ معظم تعديلات اللعب الأخرى تبدأ OFF.
- تجميد مؤقتات مهمات مدعومة وتغيير الإضاءة/السماء الأصلية. دعم Paranoia /
  TheSneakout محدود بمؤقتاتها ومؤشر الشك الموثقين؛ ليس دعمًا لكل عداد أو مهمة.
- أدوات **منفصلة** لفلترة الأصول المرئية والصوتية في نسخة محلية مُحضّرة.
  ليست أزرارًا في DLL، ولا تُفعّل تلقائيًا عند تنزيل المستودع.

لا تُساوي الاختبارات الآلية اختبار لعب شاملًا. تظل بعض الميزات تجريبية،
وتوجد حالات الدعم والتحقق المفصلة في [`release.json`](release.json).
إيقاف خيارات القائمة أو تحميل `--vanilla` لا يسترجع تعديلات أصول اللعبة على القرص.

## النسخة المدعومة

**Driver: San Francisco 1.04.1114، Windows x86، وضع Offline.**

SHA-256 لملف اللعبة المطلوب:

```text
c79987b0548f7130337c3f295c8fe269e120ceddad55c1159e6f5cbc6aa26616
```

البناء الحالي: [`build/dinput8.dll`](build/dinput8.dll).
بصمته: [`build/SHA256SUMS`](build/SHA256SUMS) و[`release.json`](release.json).

## 🚀 دليل التثبيت والتشغيل لأول مرة (Quick Start Guide)

بعد تحميل ملفات المود وملفات اللعبة، اتبع الخطوات البسيطة التالية حسب نظام تشغيلك لتشغيل اللعبة مباشرة أوفلاين دون الحاجة إلى مشغّل يوبي سوفت (Ubisoft Launcher) ودون أي أخطاء تسجيل دخول.

### 📁 1. هيكل المجلدات الأساسي
قم بنسخ أو فك ضغط مجلد المود بحيث يكون اسمه `mod` بجوار ملف تشغيل اللعبة `Driver.exe` مباشرة:

```text
Driver San Francisco/
├── Driver.exe              # ملف تشغيل اللعبة الأصلي
└── mod/                    # مجلد المود
    ├── build/dinput8.dll   # ملف المود الأساسي
    ├── install.sh          # سكربت التثبيت التلقائي (لينكس)
    ├── launch.sh           # سكربت التشغيل المباشر (لينكس)
    ├── launch.cmd          # سكربت التشغيل السريع (ويندوز)
    ├── launch.command      # سكربت التشغيل (ماك)
    └── ...
```

**تنبيه:** احفظ وأغلق اللعبة دائمًا قبل تثبيت ملفات المود أو تحديثها.

---

### 🐧 تشغيل اللعبة على Linux (أوبنتو / فيدورا / Steam Deck وغيرها)

تم تجهيز المود للعمل بسلاسة تامة مع **Proton Experimental** الخاص بـ Steam، دون الحاجة لأي خطوات معقدة.

#### الخطوة الأولى: التثبيت والإعداد التلقائي (لأول مرة)
افتح الطرفية (Terminal) في مجلد اللعبة الرئيسي ونفّذ:
```bash
./mod/install.sh
```
يقوم هذا السكربت بكل شيء تلقائيًا:
1. ينسخ ملف المود `dinput8.dll` إلى مجلد اللعبة.
2. يُهيئ بيئة تشغيل اللعبة (Wine/Proton Prefix) تلقائيًا في `~/Games/driver-san-francisco` دون الحاجة لأي ضبط يدوي.
3. يُنشئ ملف تعريف اللعب بدون إنترنت (`offline.ini`) في مجلد الحفظ تلقائيًا ليتجاوز نهائيًا خطأ المطالبة بتسجيل الدخول أونلاين.

#### الخطوة الثانية: تشغيل اللعبة
من نفس المجلد، شغّل اللعبة مباشرة بالأمر:
```bash
./mod/launch.sh
```

- لفحص الجاهزية واكتشاف التوافق قبل التشغيل:
  ```bash
  ./mod/launch.sh --check
  ```
- لتشغيل اللعبة الأصلية (Vanilla) دون تفعيل المود:
  ```bash
  ./mod/launch.sh --vanilla
  ```
- لتحديد مسار Prefix أو Proton مخصص يدويًا بدل الاكتشاف التلقائي:
  ```bash
  DSF_WINEPREFIX="/path/to/existing-prefix" \
  DSF_PROTONPATH="/path/to/Proton - Experimental" \
  DSF_UMU="/path/to/umu-run" \
  ./mod/launch.sh
  ```
- النسخ الاحتياطي التلقائي لملفات الحفظ وDLL متوقف افتراضيًا، لتفعيله: `DSF_BACKUPS=1 ./mod/launch.sh`.
- دعم أيادي التحكم (مثل يد PS4 `054c:05c4`) مدمج ومفعل افتراضيًا.

---

### 🪟 تشغيل اللعبة على Windows (ويندوز 10 / 11)

1. ضع المجلد باسم `mod` بجوار `Driver.exe`.
2. شغّل الملف `mod\launch.cmd` بنقرة مزدوجة (أو عبر سطر الأوامر):
   - ينسخ السكربت ملف `dinput8.dll` تلقائيًا إلى مجلد اللعبة.
   - يُنشئ ملف `offline.ini` تلقائيًا داخل مجلد المستندات (`Documents\Ubisoft\Driver San Francisco`) لمنع ظهور أي رسائل تطلب تسجيل الدخول عبر مشغّل Ubisoft.
3. يمكنك بعد ذلك تشغيل اللعبة مباشرة من `Driver.exe` أو عبر `mod\launch.cmd`.
- البناء الجاهز لا يحتاج تثبيت Python أو أدوات إضافية على ويندوز.

---

### 🍏 تشغيل اللعبة على macOS (عبر CrossOver أو Wine)

*ملاحظة: هذا المسار تشغيلي عبر طبقة توافق Wine/CrossOver لنظام macOS بدعم تطبيقات 32 بت.*

1. ضع المجلد `mod` داخل مجلد اللعبة.
2. افتح الطرفية (Terminal) في مجلد اللعبة وشغّل:
```bash
CX_BOTTLE="DriverSF" ./mod/launch.command
```
أو عبر تحديد مسار Wine المخصص:
```bash
DSF_WINE="/path/to/wine" WINEPREFIX="/path/to/existing-prefix" ./mod/launch.command
```
يقوم السكربت تلقائيًا بتثبيت ملف DLL وإعداد ملف الـ offline لتشغيل اللعبة بدون اتصال.

---

### 🎮 استخدام قائمة المود وميزاتها (F1)
- بمجرد تشغيل اللعبة وظهور الشاشة الرئيسية، اضغط على مفتاح **F1** لفتح أو إغلاق قائمة المود (Overlay).
- القائمة تدعم التحكم الكامل بالفأرة ولوحة المفاتيح وأيادي التحكم (Gamepads).
- **زر تختيم اللعبة 100%:** في تبويب الإعدادات (Settings)، أضفنا زرًا واحدًا يقوم بتختيم اللعبة بنسبة 100% فورًا وبشكل آمن:
  - إكمال جميع المهام الرئيسية ومهام القصة وفصول اللعبة.
  - إكمال وفتح جميع التحديات والأنشطة وتحديات الأفلام (Movie Challenges).
  - إكمال جميع التحديات الجريئة (Dares).
  - تجميع وفتح جميع رموز الأفلام (Movie Tokens) الـ 80 الموزعة في شوارع المدينة.
  - فتح وشراء جميع الكراجات وجميع السيارات (151 سيارة).
  - ترقية جميع القدرات (Boost, Ram, Zap) إلى أقصى مستوى وفتح قوة الإرادة والتحكم الكامل بالـ Shift.
  - حفظ التقدم تلقائيًا في ملف الحفظ (Autosave).
- **ضبط جودة الجرافيكس التلقائي:** يقوم المود ومشغلاته بتهيئة ملف `graphics.ini` بأعلى دقة (1080p - 1920x1080) وأقصى جودة عرض (RenderQuality 2) وتنعيم الحواف (AntiAliasing 2) لضمان وضوح ونقاء الصورة ومنع انخفاض الدقة.

## الفلاتر المحلية الاختيارية

### فلتر الأصول المرئية

المصدر والاختبارات وقوائم الاختيار في [`asset_filter/`](asset_filter/README_AR.md).
يشمل استبدال صور مختارة وإخفاء هندسة مختارة وملحقًا لفيديوهات مقدمات المهمات؛
ليس استبدالًا عامًا لكل الشخصيات ولا حذفًا لمنطقها. البدائل تستخرج محليًا من اللعبة.

### فلتر الصوت — السياسة الثانية

المصدر في [`audio_filter/`](audio_filter/README_AR.md). يحدد الموسيقى بمراجع
البنوك وMusicTrack، ويحدد الحوارات النسائية بوسوم المتحدث الأخيرة. لا يكتم
مؤثرات التصادم والتنقل في الواجهة أو التسجيلات غير المحسومة تلقائيًا.
سياسة الفيديو الحالية تزيل صوت المسارات المختلطة كاملًا، بما فيه أصوات أخرى.

**تنبيه لمن ينزّل من GitHub:** الفلتران مصدر وأدوات محلية، وليسا حزمة تفعيل
مكتملة من clone نظيف. إعداداتهما المحضّرة ونسخهما الاحتياطية تتبع تثبيتًا بعينه،
ولا تُنشر. فلتر الصور يحتاج جرد DSF_Assets خاصًا، والصوت يحتاج جرد DSF_Audio
وأصوله المستخرجة. أدوات الاستخراج الكاملة ليست ضمن هذا المستودع. لا تستخدم خطة
جهاز آخر ولا تعتبر تطابق Driver.exe وحده إثباتًا لتطابق كل الأرشيفات.
أدوات إدارة الفلاتر الحالية تعتمد على Linux و`fcntl` و`/proc`؛ تعليمات تشغيل DLL
على Windows/macOS لا تعني دعم هذه الأدوات عليهما.

لنسخة **محلية مُحضّرة مسبقًا فقط**، ومن مجلد اللعبة:

```bash
python3 mod/audio_filter/manage.py verify
python3 mod/audio_filter/manage.py restore   # استرجاع صوت ما قبل التعديل
python3 mod/audio_filter/manage.py apply     # إعادة السياسة الحالية المُحضّرة
```

استرجع فلتر الصوت قبل تغيير فلتر الصور/الفيديو السابق الذي بُني الصوت على حالته.
إذا حذفت نسخ الاسترجاع وخططها، لن تستطيع هذه الأدوات إعادة الأصول السابقة؛
ستحتاج مصدرًا أصليًا آخر أو تحقق ملفات اللعبة. لا ترفع تلك الأصول أصلًا.

## البناء والاختبار

يلزم Zig وأداة Lua 5.1 للبناء. التبعيات المطلوبة من ImGui وMinHook والخط مرفقة
بتراخيصها. توليد العربية مجددًا يحتاج GNU FriBidi؛ الرؤوس المولدة الحالية محفوظة.

من مجلد `mod`:

```bash
./build.sh
python3 -m unittest discover -s tools -p 'test_*.py' -v
python3 audio_filter/test_manage.py
python3 asset_filter/test_manage.py
python3 asset_filter/test_video_filter.py
```

اختبارات C++ تحتاج g++ بدعم 32 بت وUBSan، واختبارات Lua تحتاج Lua 5.1.
اختبارات هندسة الصور تحتاج numpy. تعتمد اختبارات أخرى على التبعيات المتاحة وقد
تُتخطى عند غيابها. الاختبارات تستعمل محاكاة وملفات مؤقتة ولا تثبت سلوك اللعب كله.
لا تُوزع نماذج سكربتات خاصة مستخرجة من اللعبة؛ بعض الاختبارات الاختيارية تقرأ
نسخة يملكها المطور إذا عُيّن `DSF_GAME_EXE` صراحة.

## النشر على GitHub

**لا تضغط مجلد التطوير المحلي كله أو ترفعه بالسحب والإفلات.** قد يتضمن
نسخًا احتياطية وأرشيفات مُحضّرة لازمة للاسترجاع. `.gitignore` لا يفلتر ZIP يدويًا
ولا يمحو ملفات سبق تتبعها أو تاريخ Git.

أنشئ نسخة النشر بقائمة سماح من مجلد `mod`:

```bash
python3 tools/export_github.py --output /path/to/new-release/mod --zip
python3 tools/export_github.py --check /path/to/new-release/mod
```

ترفض الأداة استبدال مجلد موجود أو تضمين الروابط الرمزية والملفات الخاصة، وتتحقق
من بصمة DLL والتراخيص. تشمل الكود والبناء الحالي والتبعيات والإعداد المثال فقط؛
لا تشمل اللعبة أو الصوت/الصور المستخرجة أو الحفظ أو خطط الجهاز وسجلاته.
راجع [`PUBLISHING_AR.md`](PUBLISHING_AR.md) قبل الرفع.

## التراخيص

احتفظ بـ[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) وتراخيص ImGui وMinHook
وDejaVu مع المصدر والبناء. **لم يُختَر ترخيص مستقل لكود المود المؤلف**؛ إتاحة
المصدر على GitHub وحدها لا تجعله مفتوح المصدر بترخيص محدد، ولا تمنح حقوق أصول اللعبة.

---

## English summary

An offline **Windows x86** mod menu for Driver: San Francisco 1.04.1114. Requires a
legitimately owned installation; no game executable, assets, audio, saves or ownership
bypass is distributed. Install this repository as `mod` beside your `Driver.exe`.
Close the game before installation/updates. Use `launch.sh` on Linux with existing
UMU/Proton Experimental/prefix, `launch.cmd` on Windows, or the experimental
Wine/CrossOver `launch.command` wrapper on macOS. Press **F1** for the menu.

Speed targets and mission/world controls are experimental and not verified for every
vehicle or mission. `--vanilla` bypasses the DLL, not persistent game-asset edits.
Visual/audio filter tools require private installation-specific extraction inventories;
their generated inputs, backups and prepared assets are **not** public release files.
A complete fresh-clone filter bootstrap has not been packaged.

Build with `./build.sh` (Zig and Lua 5.1); run the commands above for host/mock tests.
Export using `python3 tools/export_github.py --output /new/path/mod --zip`, rather than
zipping your development directory. Keep third-party license notices. No independent
license has been selected for the authored mod code; see `THIRD_PARTY_NOTICES.md`.
