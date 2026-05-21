#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
  cd "${BUILD_WORKSPACE_DIRECTORY}"
fi

python3 tools/refresh_compile_commands.py "$@"
