#!/usr/bin/env bash
# 웹 플레이어 빌드 (Git Bash): Web/build.sh [Release|Debug] [대상=nova|host]
#  - Emscripten = .NET wasm-tools 의 3.1.56 (Tools/web/emenv.sh), 생성기 = Visual Studio 의 Ninja
#  - nova: 엔진만 (C# 없이) → Web/build/<구성>/nova.js · nova.wasm
#  - host: C# 이 있는 게임용 — 엔진 정적 라이브러리 + .NET 웹어셈블리 런타임을 한 wasm 으로 (Web/Host, dotnet publish)
#          → Web/build/<구성>/host/wwwroot/_framework (Binaries/Scripting/NovaScriptCore.dll 이 먼저 있어야 한다 — PC 빌드)
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
if [ "$TARGET" = "host" ]; then
    "$CMAKE" --build "$OUT" --target nova_web_host -- -k 0
    # .NET 은 자기 Emscripten 환경 (같은 3.1.56 · 고정 캐시) 으로 링크한다 — 위의 환경 변수를 넘기지 않는다
    env -u EM_CACHE -u EMSDK_PATH -u FROZEN_CACHE -u DOTNET_EMSCRIPTEN_LLVM_ROOT -u DOTNET_EMSCRIPTEN_BINARYEN_ROOT -u DOTNET_EMSCRIPTEN_NODE_JS -u EMSDK_PYTHON \
        dotnet publish "$ROOT/Web/Host/NovaWebHost.csproj" -c "$CONFIG" -nologo -v:m -p:NovaWebBuildDir="$(cygpath -m "$OUT")/" -o "$OUT/host"
else
    "$CMAKE" --build "$OUT" --target "$TARGET" -- -k 0
fi
