#!/usr/bin/env bash
# Проверяет, загрузится ли плагин в среде flatpak-Reaper (D-019, D-076): версии, которые он
# требует от системных библиотек, не должны быть новее, чем в этой среде.
# Запуск: scripts/checkLibVersions.sh <файл.clap> [ИМЯ=версия ...]
# Аргументы ИМЯ=версия заменяют границу, так тесты убеждаются, что проверка умеет падать.
set -euo pipefail

# objdump переводит заголовки: в русской локали разбор ничего бы не нашёл
export LC_ALL=C

# Среда org.freedesktop.Platform 25.08, как их снять заново, написано в docs/VERIFICATION.md.
# Границы записаны здесь, потому что в CI нет flatpak. Среда со временем только новеет, поэтому
# устаревшая граница даст лишнее падение, но не пропустит поломку.
declare -A limits=([GLIBC]=2.42 [GLIBCXX]=3.4.34 [CXXABI]=1.3.15 [GCC]=14.0.0)
abiTags=" GLIBC_ABI_DT_RELR GLIBC_ABI_DT_X86_64_PLT GLIBC_ABI_GNU2_TLS "

file=$1
shift
for limit in "$@"; do
  limits[${limit%%=*}]=${limit#*=}
done

fail() {
  echo "checkLibVersions: $1" >&2
  exit 1
}

# Загрузчик смотрит на этот раздел файла, а не на отдельные символы
if ! headers=$(objdump -p "$file" 2>&1); then
  fail "не удалось прочитать $file: $headers"
fi
required=$(sed -n '/^Version References/,/^$/p' <<<"$headers" | awk '$1 ~ /^0x/ { print $NF }')
if [[ -z $required ]]; then
  fail "не удалось прочитать $file: нет требуемых версий"
fi

problems=0
for name in $required; do
  if [[ $abiTags == *" $name "* ]]; then
    continue
  fi
  if [[ ! $name =~ ^([A-Z]+)_([0-9.]+)$ || ! -v limits[${BASH_REMATCH[1]}] ]]; then
    echo "checkLibVersions: неизвестное требование $name" >&2
    problems=1
    continue
  fi
  version=${BASH_REMATCH[2]}
  limit=${limits[${BASH_REMATCH[1]}]}
  if [[ $(printf '%s\n%s\n' "$version" "$limit" | sort -V | tail -n 1) != "$limit" ]]; then
    echo "checkLibVersions: $name новее, чем разрешено ($limit)" >&2
    problems=1
  fi
done

exit $problems
