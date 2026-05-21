#!/usr/bin/env bash
set -euo pipefail

source_file="$1"
compiler="${CXX:-c++}"
extra_flags=()

if [[ "$(uname -s)" == "Darwin" ]]; then
  extra_flags+=("-fexperimental-library")
fi

tmp_obj="${TEST_TMPDIR:-/tmp}/cross_type_assignment_fails.o"
if "${compiler}" -std=c++20 -Isrc "${extra_flags[@]}" -c "${source_file}" -o "${tmp_obj}"; then
  echo "expected cross_type_assignment_fails.cpp to fail compilation" >&2
  exit 1
fi
