#!/usr/bin/env bash
#
# jText test runner — builds and tests both self-contained version worlds:
#   v001 (C++23) and v002 (C++26).
#
# Each version world is independent (cd v001 / cd v002 and use its own CMake).
# This driver just does both. The DB->jText round-trip in jtext_from_sql needs a
# live PostgreSQL; its ctest only checks argument handling otherwise (skipped,
# never a hard failure).
#
# Usage:
#   ./run-tests.sh                 # textual build + ctest for both versions
#   ./run-tests.sh --modules       # also build + test the C++ module front-ends
#   CXX=clang++ ./run-tests.sh     # force a compiler
#
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
MOD=0
for a in "$@"; do case "$a" in --modules) MOD=1 ;; *) echo "unknown arg: $a"; exit 2 ;; esac; done

CXX_ARG=()
[ -n "${CXX:-}" ] && CXX_ARG=(-DCMAKE_CXX_COMPILER="$CXX")

for v in v001 v002; do
  echo "=================== jText $v ==================="
  cmake -G Ninja -S "$ROOT/$v" -B "$ROOT/$v/build" -DCMAKE_BUILD_TYPE=Debug "${CXX_ARG[@]}"
  cmake --build "$ROOT/$v/build"
  ctest --test-dir "$ROOT/$v/build" --output-on-failure
  if [ "$MOD" = 1 ]; then
    echo "------------------- $v modules -------------------"
    cmake -G Ninja -S "$ROOT/$v" -B "$ROOT/$v/build-mod" -DCMAKE_BUILD_TYPE=Debug -DJTEXT_BUILD_MODULES=ON "${CXX_ARG[@]}"
    cmake --build "$ROOT/$v/build-mod"
    ctest --test-dir "$ROOT/$v/build-mod" --output-on-failure -R module
  fi
done
echo "All jText version worlds built and tested."
