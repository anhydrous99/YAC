#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
  cd "${BUILD_WORKSPACE_DIRECTORY}"
fi

if command -v run-clang-tidy-21 >/dev/null 2>&1; then
  run_clang_tidy="run-clang-tidy-21"
elif command -v run-clang-tidy >/dev/null 2>&1; then
  run_clang_tidy="run-clang-tidy"
else
  echo "run-clang-tidy not found; install run-clang-tidy-21 or run-clang-tidy" >&2
  exit 127
fi

python3 tools/refresh_compile_commands.py

files=()
while IFS= read -r file; do
  files+=("${file}")
done < <(find src tests -type f \( -name '*.cpp' -o -name '*.hpp' \) | sort)

"${run_clang_tidy}" \
  -p . \
  -header-filter='^(src|tests)/' \
  -j 0 \
  "${files[@]}"
