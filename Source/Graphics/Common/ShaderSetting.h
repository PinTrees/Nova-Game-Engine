#pragma once

struct ShaderSetting
{
	ShaderSetting() { ZeroMemory(this, sizeof(this)); }

	//int LightCount;
	int UseTexture = false;
	int AlphaClip = false;
	int UseNormalMap = false;
	int UseShadowMap = true;   // 새 재질도 기본으로 그림자를 받는다
	int UseSsaoMap = false;
	int ReflectionEnabled = false;
	int FogEnabled = false;

	// 16����Ʈ ����
	//bool pad1;
	//XMFLOAT2 pad2;

	int pad;
};
