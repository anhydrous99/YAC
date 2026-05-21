#!/usr/bin/env bash
set -euo pipefail

if [[ -n "${BUILD_WORKSPACE_DIRECTORY:-}" ]]; then
  cd "${BUILD_WORKSPACE_DIRECTORY}"
fi

bazel coverage --config=coverage //tests:all_tests

report="bazel-out/_coverage/_coverage_report.dat"
if [[ -f "${report}" ]]; then
  echo "Coverage report: ${report}"
fi
