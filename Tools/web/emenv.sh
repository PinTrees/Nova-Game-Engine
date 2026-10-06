# 웹 빌드용 Emscripten 환경 — .NET wasm-tools 워크로드에 든 Emscripten (C# 런타임과 같은 버전이라 한 wasm 으로 묶을 수 있다)
#   source Tools/web/emenv.sh   →  emcc · em++ · emar
#   캐시는 쓸 수 있는 곳 (%LOCALAPPDATA%\NovaWeb\emcache — 처음 한 번 dotnet packs 에서 복사)
NOVA_DOTNET_PACKS="${NOVA_DOTNET_PACKS:-/c/Program Files/dotnet/packs}"
NOVA_EM_VER="${NOVA_EM_VER:-3.1.56}"
_em_pack() { ls -d "$NOVA_DOTNET_PACKS"/Microsoft.NET.Runtime.Emscripten.$NOVA_EM_VER.$1.win-x64/*/ 2>/dev/null | sort -V | tail -1; }
_EM_SDK="$(_em_pack Sdk)tools"
export EMSDK_PATH="$_EM_SDK/"
export DOTNET_EMSCRIPTEN_LLVM_ROOT="$_EM_SDK/bin"
export DOTNET_EMSCRIPTEN_BINARYEN_ROOT="$_EM_SDK/"
export DOTNET_EMSCRIPTEN_NODE_JS="$(_em_pack Node)tools/bin/node.exe"
export EMSDK_PYTHON="$(_em_pack Python)tools/python.exe"
export EM_CACHE="${NOVA_EM_CACHE:-$HOME/.nova/emcache}"
export FROZEN_CACHE=
if [ ! -d "$EM_CACHE/sysroot" ]; then
    mkdir -p "$EM_CACHE" && cp -r "$(_em_pack Cache)tools/emscripten/cache/." "$EM_CACHE/"
fi
export PATH="$_EM_SDK/emscripten:$_EM_SDK/bin:$(dirname "$EMSDK_PYTHON"):$PATH"
