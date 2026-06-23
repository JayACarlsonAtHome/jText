#!/usr/bin/env bash
#
# jText standalone build + test runner.
#
# The library + tools here are kept identical to jac313's in-tree jText package,
# but this repo has no jac313_test_cli driver — so this is the self-contained way
# to configure, build, and run the test suite (library + tools).
#
# Usage:
#   ./run-tests.sh                 # configure (Debug) -> build -> ctest
#   CXX=clang++ ./run-tests.sh     # force a compiler
#   ./run-tests.sh -DJTEXT_BUILD_TOOLS=OFF   # extra cmake args pass through
#
# jText builds at cxx_std_26, so a recent toolchain is required (g++-15 / gcc
# toolset 15 / Clang 20+). The DB->jText round-trip in jtext_from_sql needs a
# live PostgreSQL; its ctest only checks argument handling (round-trip skipped
# without psql + a target DB).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BUILD="${ROOT}/build"

# --- pick an activation prefix + C++ compiler (mirrors jac313 bootstrap) ---
ACT=""
CXX="${CXX:-}"
if [ -z "$CXX" ]; then
    if   command -v g++-15 >/dev/null 2>&1;            then CXX=g++-15
    elif command -v gcc-toolset-15-env >/dev/null 2>&1; then ACT="gcc-toolset-15-env"; CXX=g++
    elif command -v g++ >/dev/null 2>&1;               then CXX=g++
    elif command -v clang++ >/dev/null 2>&1;           then CXX=clang++
    else echo "run-tests: no C++ compiler found (need g++-15 / gcc-toolset-15 / clang)" >&2; exit 1
    fi
fi

echo "jText runner: compiler ${ACT:+[$ACT] }$CXX   build dir: $BUILD"
$ACT cmake -S "$ROOT" -B "$BUILD" -DCMAKE_CXX_COMPILER="$CXX" -DCMAKE_BUILD_TYPE=Debug "$@"
$ACT cmake --build "$BUILD" -j
$ACT ctest --test-dir "$BUILD" --output-on-failure
