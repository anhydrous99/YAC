#!/usr/bin/env bash
# Verify that the vcpkg submodule commit matches the baseline in vcpkg-configuration.json.
# Prevents silent drift that can cause vcpkg to resolve different package versions than
# expected. Run manually before committing changes to external/vcpkg or
# vcpkg-configuration.json, and in CI.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

if [ ! -d external/vcpkg ]; then
  echo "ERROR: external/vcpkg submodule missing. Run: git submodule update --init --recursive"
  exit 1
fi

SUBMODULE_SHA=$(cd external/vcpkg && git rev-parse HEAD)
CONFIG_BASELINE=$(python3 -c "import json; print(json.load(open('vcpkg-configuration.json'))['default-registry']['baseline'])")

if [ "$SUBMODULE_SHA" != "$CONFIG_BASELINE" ]; then
  echo "ERROR: vcpkg submodule commit ($SUBMODULE_SHA) does not match"
  echo "       vcpkg-configuration.json baseline ($CONFIG_BASELINE)"
  echo ""
  echo "To fix: update vcpkg-configuration.json to set baseline = $SUBMODULE_SHA"
  echo "        or check out the matching submodule commit"
  exit 1
fi

echo "OK: baseline matches submodule commit ($SUBMODULE_SHA)"
