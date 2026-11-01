#!/usr/bin/env bash
# Полная проверка одной командой, ту же команду выполняет CI (D-071).
# Предупреждение считается ошибкой: иначе оно видно только при пересборке файла.
set -euo pipefail

cd "$(dirname "$0")/.."

cmake -S . -B build -G Ninja -DSINT_WERROR=ON
cmake --build build
ctest --test-dir build --output-on-failure
