# Vulkan 헤더 (Khronos)

- 출처: LunarG Vulkan SDK 1.4.363.0 의 `Include/vulkan` · `Include/vk_video` (원본: https://github.com/KhronosGroup/Vulkan-Headers)
- 라이선스: Apache-2.0 OR MIT (각 파일 머리의 SPDX — Copyright The Khronos Group Inc.)
- C 헤더만 넣었다 (C++ `vulkan.hpp` 계열 제외). 엔진은 `VK_NO_PROTOTYPES` 로 함수를 `vulkan-1.dll` (그래픽 드라이버에 포함) 에서 실행 중에 불러온다 — 엔진을 빌드 · 실행하는 데 Vulkan SDK 가 필요 없다
- 검증 레이어 (`VK_LAYER_KHRONOS_validation`) 는 개발 PC 에 SDK 가 있으면 Debug 빌드에서 켠다 (없으면 그냥 넘어감)
