# astc-encoder (NOVA 사본)

- 출처: https://github.com/ARM-software/astc-encoder 태그 **5.7.0** 의 `Source/astcenc_*.cpp · .h` (고치지 않음)
- 라이선스: Apache-2.0 ([LICENSE.txt](LICENSE.txt))
- 쓰는 곳: `Source/Build/TextureCompressor.cpp` — 안드로이드 게임 데이터 내보내기 (`nova android export`) 에서 그림을 ASTC 로 굽는다 (에디터만, 기기에는 안 들어감)
- 빌드: 루트 `CMakeLists.txt` 의 정적 라이브러리 `astcenc` (SSE 4.1 · POPCNT, AVX2 · F16C 끔 — 오래된 PC 에서도 돌게)
- 올릴 때: 같은 파일 목록을 새 태그에서 내려받아 덮어쓰고 `nova android export --texture-compression astc` 의 PSNR 을 비교
