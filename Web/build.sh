#!/usr/bin/env bash
# 웹 플레이어 빌드 (Git Bash): Web/build.sh [Release|Debug] [대상=nova]
#  - Emscripten = .NET wasm-tools 의 3.1.56 (Tools/web/emenv.sh), 생성기 = Visual Studio 의 Ninja
#  - 결과: Web/build/<구성>/nova.js · nova.wasm (+ 정적 라이브러리 — C# 이 있는 게임은 .NET 이 다시 링크한다)
set -e
CONFIG="${1:-Release}"
TARGET="${2:-nova}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
source "$ROOT/Tools/web/emenv.sh"
VS="$(ls -d "/c/Program Files/Microsoft Visual Studio/"*/*/ 2>/dev/null | sort -V | tail -1)"
CMAKE="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
NINJA="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe"
OUT="$ROOT/Web/build/$CONFIG"
if [ ! -f "$OUT/build.ninja" ]; then
    "$CMAKE" -S "$ROOT/Web" -B "$OUT" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA" -DCMAKE_BUILD_TYPE="$CONFIG" \
        -DCMAKE_TOOLCHAIN_FILE="$EMSDK_PATH/emscripten/cmake/Modules/Platform/Emscripten.cmake"
fi
"$CMAKE" --build "$OUT" --target "$TARGET" -- -k 0
