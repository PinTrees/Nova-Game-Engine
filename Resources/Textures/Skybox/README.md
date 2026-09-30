# Skybox

| 파일 | 원본 | 라이선스 |
|---|---|---|
| KloofendalPureSky.dds | Poly Haven — [Kloofendal 48d Partly Cloudy (Pure Sky)](https://polyhaven.com/a/kloofendal_48d_partly_cloudy_puresky) 2K HDR (Greg Zaal, Jarod Guest) | CC0 |

`Tools/hdri_to_cubemap.py` 로 변환했다 (512 큐브, 밉 10단계, R9G9B9E5 HDR, 감마 공간 값).
노출은 위 반구 밝기 중간값 0.38, 태양은 12 에서 자른다 — 더 밝으면 기본 Volume 의 Bloom(threshold 0.9)이 구름 전체를 번지게 해 화면이 뿌옇게 된다.

```
python Tools/hdri_to_cubemap.py kloofendal_48d_partly_cloudy_puresky_2k.hdr Resources/Textures/Skybox/KloofendalPureSky.dds --size 512 --sky-median 0.38 --clamp 12
```

스카이박스 배경(Game 뷰 카메라 Background = Skybox, Scene 뷰 툴바 Effects > Skybox), Lit 머티리얼의 반사(gCubeMap), 환경광(가장 흐린 밉)에 함께 쓴다.
