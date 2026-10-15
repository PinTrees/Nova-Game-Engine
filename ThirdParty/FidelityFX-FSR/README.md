# AMD FidelityFX Super Resolution 1.0

- 출처: https://github.com/GPUOpen-Effects/FidelityFX-FSR (`ffx-fsr/ffx_fsr1.h`), MIT (LICENSE.txt)
- 쓰는 곳: `Shaders/68. Upscale.fx` — EASU (`FsrEasuF`) · RCAS (`FsrRcasF`) 의 32 비트 경로를 HLSL 로 옮겼다 (헤더 파일 자체는 넣지 않음)
- 바꾼 점: gather 대신 Texture.Load 12 번 (OpenGL · Vulkan · WebGPU 변환), 근사 역수 (`APrxLoRcpF1` 등) 대신 0 을 막은 나눗셈
