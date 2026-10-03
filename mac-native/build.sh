#!/usr/bin/env bash
# Переносная сборка учебного проекта с windows.h + GDI.
# Исходники по умолчанию находятся рядом с папкой модуля.
set -euo pipefail
shopt -s nullglob

SELF_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$SELF_DIR/.."
RUN_AFTER_BUILD=0
SOURCE_ARGS=()

usage() {
    printf '%s\n' \
        "Сборка: ./mac-native/build.sh [опции] [исходники]" \
        "Запуск: ./mac-native/run.sh [опции] [исходники]" \
        "" \
        "  --project ПАПКА  Собрать другой проект, не копируя модуль" \
        "  --run            Собрать и сразу запустить" \
        "  --help           Показать справку" \
        "" \
        "Без списка файлов собираются .cpp/.cc/.cxx/.C в корне проекта." \
        "Пути исходников отсчитываются от папки проекта." \
        "Если main() несколько: ./mac-native/run.sh main.cpp Point.cpp"
}

fail() {
    echo "$*" >&2
    exit 1
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --run)
            RUN_AFTER_BUILD=1
            shift
            ;;
        --project)
            [[ $# -ge 2 ]] || fail "После --project укажите папку проекта."
            [[ -n "$2" ]] || fail "Папка проекта не может быть пустой."
            PROJECT_DIR="$2"
            shift 2
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        --)
            shift
            SOURCE_ARGS+=("$@")
            break
            ;;
        -*)
            fail "Неизвестная опция: $1. Справка: --help"
            ;;
        *)
            SOURCE_ARGS+=("$1")
            shift
            ;;
    esac
done

[[ -d "$PROJECT_DIR" ]] || fail "Папка проекта не найдена: $PROJECT_DIR"
PROJECT_DIR="$(cd "$PROJECT_DIR" && pwd)"
cd "$PROJECT_DIR"

[[ "$(uname -s)" == Darwin ]] || fail "Этот модуль предназначен для macOS."
if ! CXX_BIN="$(xcrun --find clang++)"; then
    fail "Нужны Xcode Command Line Tools: xcode-select --install"
fi
SDK_DIR="$(xcrun --sdk macosx --show-sdk-path)"

SOURCES=()
if [[ ${#SOURCE_ARGS[@]} -gt 0 ]]; then
    SOURCES=("${SOURCE_ARGS[@]}")
else
    SOURCES=("$PROJECT_DIR"/*.cpp "$PROJECT_DIR"/*.cc "$PROJECT_DIR"/*.cxx "$PROJECT_DIR"/*.C)
fi
[[ ${#SOURCES[@]} -gt 0 ]] || fail "В $PROJECT_DIR нет .cpp/.cc/.cxx/.C. Укажите исходники аргументами."
for src in "${SOURCES[@]}"; do
    [[ -f "$src" ]] || fail "Исходный файл не найден: $src"
    case "$src" in
        *.cpp|*.cc|*.cxx|*.C) ;;
        *) fail "Ожидался файл C++ (.cpp/.cc/.cxx/.C): $src" ;;
    esac
done

PROJECT_NAME="$(basename "$PROJECT_DIR")"
OUT="$SELF_DIR/${PROJECT_NAME}_native"
BUILD_DIR="$SELF_DIR/.build"
mkdir -p "$BUILD_DIR"

echo "Проект:  $PROJECT_DIR"
echo "Файлы:   ${SOURCES[*]}"

"$CXX_BIN" -isysroot "$SDK_DIR" -std=c++17 -fno-objc-arc -Wall -Wextra -Wno-deprecated-declarations \
    -c "$SELF_DIR/gdi_compat.mm" -o "$BUILD_DIR/gdi_compat.o" -I "$SELF_DIR"

OBJS=("$BUILD_DIR/gdi_compat.o")
INDEX=0
for src in "${SOURCES[@]}"; do
    obj="$BUILD_DIR/source_${INDEX}.o"
    "$CXX_BIN" -isysroot "$SDK_DIR" -std=c++17 -I "$SELF_DIR" -c "$src" -o "$obj"
    OBJS+=("$obj")
    INDEX=$((INDEX + 1))
done

if ! "$CXX_BIN" -isysroot "$SDK_DIR" "${OBJS[@]}" -framework Cocoa -o "$OUT"; then
    fail "Не удалось собрать программу. Если в папке несколько main(), перечислите исходники нужной программы."
fi

echo "Готово: $OUT"
if [[ "$RUN_AFTER_BUILD" -eq 1 ]]; then
    echo "Запуск..."
    exec "$OUT"
fi
echo "Собрать и запустить: \"$SELF_DIR/run.sh\""
