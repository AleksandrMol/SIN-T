# Как проверять SIN-T

Рецепты для этой машины: Fedora 44, сессия Wayland, Reaper во flatpak. Что запускать для какой правки, написано в `CLAUDE.md`.

## Окно редактора без DAW

`Xvfb` и `xdotool` не установлены, поэтому окно проверяется на настоящем дисплее.

```sh
./build/sint_editor_preview out.ppm [--open] [--preset файл] [--scale 1.5] [--resize 1000x640] [--hold 5]
python3 -I tools/editorPreview/ppmToPng.py out.ppm out.png
```

`--open` показывает подграф первого генератора, `--hold` держит окно открытым для событий снаружи. Инструмент печатает, сколько кадров нарисовано за 0,5 с простоя: должно быть 0.

Обязательный набор кадров при правке вида (D-073): без флагов, `--open`, `--scale 1.5`, `--resize 800x520`.

События мыши отправляются через `XSendEvent` в дочернее окно с классом `SintEditor`, pugl такие события принимает. Пример есть в `prototypes/gui-rmlui/test_host.cpp`. Этот хост открывает и закрывает окна любого `.clap` и показывает расход памяти:

```sh
cmake -S prototypes/gui-rmlui -B build-proto -G Ninja
cmake --build build-proto
./build-proto/test_host build/sint.clap <папка для кадров> [число переоткрытий]
```

## Reaper

Reaper установлен через flatpak (`fm.reaper.Reaper`), видит домашнюю папку, работает через X11. `/tmp` у него свой, поэтому файлы для проверки класть в домашнюю папку.

- Открытый Reaper принадлежит пользователю: перед запуском проверить `flatpak ps`, закрывать его и слать ему команды нельзя. `cmake --install` при открытом Reaper безопасен: файл заменяется новым, загруженная копия работает дальше и в `/proc/<pid>/maps` видна как `(deleted)`.
- Зависимости в среде Reaper после каждой установки: `flatpak run --command=sh fm.reaper.Reaper -c 'ldd ~/.clap/sint.clap | grep "not found"'`. Вывода быть не должно.
- Запуск дописывает данные в `reaper.ini` (`~/.var/app/fm.reaper.Reaper/config/REAPER/`), файл нужно сохранить заранее и вернуть. Окно закрывается через `flatpak kill fm.reaper.Reaper`.
- Скрипт с живым плагином: `flatpak run fm.reaper.Reaper -nosplash -new <скрипт.lua>`. Плагин добавляется через `TrackFX_AddByName(track, "SIN-T", false, -1)`, окно открывается `TrackFX_Show(track, fx, 3)`. Запись движения ручки видна по `GetLastTouchedFX`, `CountTrackEnvelopes` и `<PARMENV` в `GetTrackStateChunk` при режиме автоматизации touch.
- Живой звук: нота идёт через `reaper.StuffMIDIMessage` на виртуальную клавиатуру (перед нотой дождаться ~1 с после включения записи на дорожке), уровень читается через `reaper.Track_GetPeakInfo`. Клик по пианоролу так не проверить.
- Рендер: `flatpak run fm.reaper.Reaper -renderproject <проект.RPP>`, в проекте заполнить `RENDER_FILE` и `RENDER_FMT`. Подготовить можно скриптом: `GetSetProjectInfo_String` с `RENDER_FILE`, `RENDER_PATTERN`, `RENDER_FORMAT` (`ZXZhdxgAAQ==` — WAV 24 бита) и `GetSetProjectInfo` с `RENDER_BOUNDSFLAG` = 1. Модуль `wave` такой WAV читает.
- Состояние: у CLAP-плагинов в Reaper нет `vst_chunk`, `TrackFX_Get/SetNamedConfigParm` возвращают `false`. Состояние меняется через `SetTrackStateChunk`, блок `<STATE` — base64 от JSON состояния. Проект с другим пресетом собирается подстановкой этого блока. В блоке проекта у плагина с `clap.state` нет строк `P`, значения хранит только наше состояние.
- Огибающая параметра хранится как `<PARMENV <слот>:<слот> ...>`. `GetFXEnvelope` не видит и не создаёт огибающие скрытых параметров.
- В списке Reaper 1035 параметров: наши 1032 плюс Bypass, Wet, Delta.
- Изменение размера окна проверяется только мышью: мышью работает, а `XResizeWindow` снаружи до `set_size` плагина не доходит.

`sint_render` пишет WAV с плавающей точкой (формат 3). Модуль `wave` в Python такой файл не читает, данные разбирать вручную через `struct` и `array`.

## Версии библиотек

`scripts/checkLibVersions.sh` входит в `ctest`. Границы в нём сняты со среды Reaper (`org.freedesktop.Platform` 25.08). Если у Reaper сменилась среда, снять заново:

```sh
L=$(flatpak info -l org.freedesktop.Platform//25.08)/files/lib/x86_64-linux-gnu
LC_ALL=C objdump -p $L/libstdc++.so.6 | sed -n '/^Version definitions/,/^$/p' | awk 'NR>1 {print $4}' | sort -V
```

То же для `libc.so.6` и `libgcc_s.so.1`. Без `LC_ALL=C` заголовки переведены и разбор ничего не находит. Проверка не заменяет `ldd` из раздела про Reaper: она не видит библиотеку, которой нет в среде вовсе.

## Эталоны звука

Тест `Звук совпадает с эталонами` в `sint_tests` сравнивает рендер с WAV из `tests/golden/`, список сценариев в `tools/goldenScenarios` (D-072, D-085). Если звук изменён намеренно:

```sh
cmake --build build --target update_golden
```

Команда печатает, насколько и на какой секунде разошёлся каждый сценарий. Изменились должны только сценарии, которые задевает правка: `default-chord` при правке пресета по умолчанию, `two-saws` при правке этого примера. Старый проект при правке значений по умолчанию меняться не должен (D-041). Обновлённые WAV идут в одном коммите с правкой, а задача, меняющая слышимое, вливается после проверки пользователем (D-080).

Волны осциллятора в файле нумеруются с 1, волны LFO с 0. Значение вне диапазона молча ограничивается, поэтому новый сценарий проверять мутацией: нарочно изменить то, что он должен покрывать.

Влияние процессора на libm проверяется без другой машины: `GLIBC_TUNABLES=glibc.cpu.hwcaps=-FMA,-AVX2,-AVX512F ./build/sint_tests -tc="Звук совпадает*"`.

## CI в контейнере на этой машине

Чистая машина из `.github/workflows/check.yml` воспроизводится через `podman run` с образом `fedora:44`: скопировать в папку для временных файлов файлы из `git ls-files -co --exclude-standard`, подключить её через `-v` с `--security-opt label=disable`, поставить пакеты из файла CI и запустить `scripts/check.sh`. `dnf` падает на прокси, но в контейнере работает без него: ему одному убрать `http_proxy` и `https_proxy` через `env -u`, а зависимости сборки качать с прокси, как обычно.

## Санитайзеры

```sh
cmake -S . -B build-tsan -G Ninja -DSINT_SANITIZER=thread "-DCMAKE_EXE_LINKER_FLAGS=-L<папка> -Wl,-rpath,<папка>"
cmake --build build-tsan --target sint_tests
```

То же для `address`. Сборки с санитайзером идут без редактора.

`libasan` и `libtsan` не установлены, `dnf` падает на прокси. Обе библиотеки берутся без `sudo`: скачать RPM через `curl` с `dl.fedoraproject.org/pub/fedora/linux/updates/44/Everything/x86_64/Packages/l/` (версия как у `libgcc`), проверить `rpm -K`, распаковать `rpm2cpio | cpio` во временную папку, рядом сделать символические ссылки `libtsan.so` и `libasan.so`, потому что системные файлы с этими именами ссылаются на отсутствующие пути.

Стресс-тест запускается не один раз (`-tc="*стресс-тест*"`). Зелёному прогону верить только после отрицательной проверки: намеренно внесённая в копию проекта гонка должна быть поймана. Короткая проба с настоящей гонкой уже молчала.

## Мутации

Нарочно ломать новый код по одному месту и смотреть, упадут ли тесты. Выжившую мутацию разобрать: дыра в тестах, лишний код или эквивалентная мутация.

- Ломать только копию проекта в папке для временных файлов, с `-DFETCHCONTENT_BASE_DIR` на копию `build/_deps` и `-DFETCHCONTENT_FULLY_DISCONNECTED=ON`, чтобы не скачивать зависимости.
- Если исходники копируются поверх готовой сборки, обновить их даты через `touch`, иначе `ninja` оставит объектные файлы прошлой мутации.
- Перед мутациями убедиться, что база проходит: иначе результат ничего не значит.
- Мутация, которая не собралась, ничего не проверила.

## clap-validator

Не установлен. Готовый архив есть в релизах `free-audio/clap-validator`, версия `0.4.1`, файл для `ubuntu-22.04`, контрольная сумма указана в API релизов. Значения параметров упавшего теста он пишет в `/run/user/1000/clap-validator/com.chillyswim.sin-t/`.
