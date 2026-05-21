#!/usr/bin/env bash
set -euo pipefail

mode="${1:---fix}"
if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
  cd "${BUILD_WORKSPACE_DIRECTORY}"
fi

if command -v clang-format-21 >/dev/null 2>&1; then
  clang_format="clang-format-21"
elif command -v clang-format >/dev/null 2>&1; then
  clang_format="clang-format"
else
  echo "clang-format not found; install clang-format-21 or clang-format" >&2
  exit 127
fi

files=()
while IFS= read -r file; do
  files+=("${file}")
done < <(find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) | sort)

if [[ "${#files[@]}" -eq 0 ]]; then
  exit 0
fi

case "${mode}" in
  --check | check)
    "${clang_format}" --dry-run --Werror "${files[@]}"
    ;;
  --fix | fix)
    "${clang_format}" -i "${files[@]}"
    ;;
  *)
    echo "usage: $0 [--fix|--check]" >&2
    exit 2
    ;;
esac
