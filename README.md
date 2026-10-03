# winapi-macos — windows.h и WinAPI GDI для C++ на macOS

Native macOS compatibility layer for educational C++ programs using a subset
of Windows API / GDI. Portable CLI installer for Apple Silicon and Intel Macs,
available through Homebrew.

**winapi-macos** помогает собрать и запустить на Mac учебные программы на C++,
которые рисуют через `windows.h`, `SetPixel`, `CreatePen` и `Ellipse`.
Поддерживаемые функции WinAPI/GDI работают через нативное окно Cocoa;
вывод `cout` остаётся в терминале. Для таких проектов исходный
`#include <windows.h>` менять не нужно.

[Установка через Homebrew](#установка-через-homebrew) ·
[Поддерживаемые функции](#поддерживаемые-функции-windows-api-и-gdi) ·
[Документация модуля](mac-native/README.md) ·
[Скачать релиз](https://github.com/kwtpub/winapi-macos/releases/latest)

## Установка через Homebrew

Установите библиотеку и команду `winapi-macos`:

```sh
brew install kwtpub/tap/winapi-macos
```

Для сборки C++-проектов нужны Apple Clang и macOS SDK из Xcode или
Command Line Tools. Если инструменты ещё не установлены:

```sh
xcode-select --install
```

Готовый установщик для **Apple Silicon и Intel** также доступен
в [GitHub Releases](https://github.com/kwtpub/winapi-macos/releases).
Он содержит весь переносной комплект.

## Как запустить C++-проект с windows.h на Mac

Перейдите в папку практики с исходниками и выполните:

```sh
winapi-macos --run
```

Команда добавит папку `mac-native/`, соберёт программу через Apple Clang
и сразу запустит её. Графика появится в окне Cocoa. Активируйте это окно
и нажимайте клавиши, которые ожидает ваша программа.

Для установки прослойки без сборки и запуска:

```sh
winapi-macos
```

Для установки в другую папку:

```sh
winapi-macos --dir "/путь/к/практике"
```

Установщик не изменяет `.cpp` и `.pro`. Совпадающие файлы модуля
сохраняются; замену отличающихся файлов нужно явно разрешить через
`--force`.

### Повторная сборка и несколько программ в папке

После изменения учебного кода используйте:

```sh
./mac-native/run.sh
```

Для сборки без запуска:

```sh
./mac-native/build.sh
```

По умолчанию скрипт выбирает все `.cpp`, `.cc`, `.cxx` и `.C`
в корне проекта. Если в папке несколько программ с собственным
`main()` или исходники находятся в подпапках, перечислите файлы
одной программы:

```sh
./mac-native/run.sh src/main.cpp src/Point.cpp
```

Исполняемый файл сохраняется в `mac-native/<имя-папки>_native`.
Папку модуля можно переносить между учебными проектами.

## Qt Creator и qmake на macOS

Если собираете проект в Qt Creator, добавьте после настроек `CONFIG`
в файл `.pro`:

```qmake
include($$PWD/mac-native/mac-native.pri)
```

Выберите комплект macOS и соберите проект. qmake использует список
исходников из `.pro`; подключаемый файл добавляет заголовок совместимости,
реализацию Cocoa и необходимые параметры сборки. Подробности настройки
для macOS и Windows — в [инструкции по qmake](mac-native/README.md#qt-creator-и-qmake).

## Поддерживаемые функции Windows API и GDI

Это библиотека совместимости для **подмножества WinAPI/GDI учебной графики**.

| Возможность | Функции и константы |
| --- | --- |
| Окно и контекст рисования | `GetConsoleWindow`, `GetWindowDC`, `ReleaseDC` |
| Цвета и точки | `RGB`, `SetPixel`, `CLR_INVALID` |
| Сплошные перья и контуры эллипсов | `CreatePen`, `PS_SOLID`, `SelectObject`, `Ellipse`, `DeleteObject` |
| Опрос клавиатуры активного окна | `GetAsyncKeyState` |
| Задержка с обработкой событий окна | `Sleep` |

Координаты начинаются сверху слева, Y растёт вниз. Рисунок сохраняется
при перерисовке окна. Клавиатура поддерживает цифры, физические буквы A–Z,
стрелки и другие клавиши, перечисленные в [документации](mac-native/README.md).

Вызовы прослойки должны выполняться в главном потоке. Кисти, заливка
эллипсов, пунктирные перья, глобальный опрос клавиатуры и произвольные
Win32-окна не реализованы. Для других вызовов Windows API или сторонних
зависимостей может потребоваться дополнительное портирование.

### Цвет фона и стирание фигур

Фон по умолчанию синий `RGB(0,0,255)`. Цвет, которым учебная программа
стирает фигуры, должен совпадать с фоном. Для белого фона:

```sh
env GDI_BACKGROUND=255,255,255 ./mac-native/run.sh
```

Настройки окна, отладка и режим автоматического прохождения примера —
в [документации переносного модуля](mac-native/README.md).

## Частые вопросы о windows.h на macOS

### Как исправить ошибку «windows.h file not found»?

Для учебного проекта, использующего поддерживаемые функции WinAPI/GDI,
установите модуль и запускайте сборку через `winapi-macos --run`
или `./mac-native/build.sh`. Скрипт добавляет путь к совместимому
`windows.h` и подключает реализацию Cocoa.

Одна команда `clang++ main.cpp` без этих параметров не подключает прослойку.
Добавление заголовка также не заменяет портирование функций Windows API,
которых в модуле нет.

### Работает ли библиотека на Apple Silicon (M-серия) и Intel?

Установщик содержит архитектуры arm64 и x86_64 для Apple Silicon и Intel.
Учебная программа компилируется локально компилятором Apple Clang.

### Нужны ли Wine и Qt?

Сборка через `run.sh` использует Apple Clang и Cocoa. Wine и Qt
для этого способа запуска не требуются. Интеграция с Qt Creator доступна
через файл `mac-native.pri`.

### Можно ли запустить любой C++-проект для Windows?

Совместимость ограничена перечисленными функциями учебной графики.
Проекты с Win32 GUI, другими частями Windows SDK или внешними
Windows-библиотеками требуют отдельной адаптации.

## Исходники, сборка установщика и проверки

- [windows.h для macOS](mac-native/windows.h) — доступные типы,
  константы и объявления функций.
- [Реализация WinAPI/GDI через Cocoa](mac-native/gdi_compat.mm) —
  окно, рисование и клавиатура.
- [CLI-установщик](mac-native/tools/installer.cpp) и
  [сборщик пакета](mac-native/tools/package.py).
- [Проверки переносимости](mac-native/tools/check_portability.py) и
  [графические проверки](mac-native/tests/smoke.mm).

Из корня репозитория на macOS:

```sh
python3 mac-native/tools/package.py
python3 mac-native/tools/check_portability.py --no-gui
```

Python 3 нужен для сборки установщика из исходников. Сборка учебных
программ через установленный модуль использует Apple Clang и SDK.
В `mac-native/dist/` появятся бинарник, ZIP переносного модуля,
архив релиза и `SHA256SUMS`.

CI проверяет синтаксис, обе архитектуры установщика и переносимость
без графической сессии. Проверки Cocoa можно включить параметром
`gui_smoke` при ручном запуске CI; им нужна графическая сессия macOS.

## Релизы и лицензия

- [Скачать winapi-macos](https://github.com/kwtpub/winapi-macos/releases/latest).
- [Homebrew tap](https://github.com/kwtpub/homebrew-tap).
- [История изменений](CHANGELOG.md).
- [Лицензия MIT](LICENSE).
