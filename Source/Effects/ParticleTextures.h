#pragma once
#include <string>
#include <vector>

// 입자 텍스처. 내장은 실행 중에 만든다 (Unity 의 Default-Particle 과 같은 역할), 그 외는 Assets 이미지 (UISprites 로 읽음).
//  builtin:Default-Particle  부드러운 원 (기본)
//  builtin:Glow              가운데가 밝고 멀리 퍼지는 빛 (Additive 용)
//  builtin:Smoke             뭉게뭉게한 연기 덩어리
//  builtin:Spark             가느다란 십자 반짝임
//  builtin:Flame-Sheet       4x4 불꽃 플립북 (Texture Sheet Animation 4 x 4 로 쓴다)
namespace ParticleTextures
{
	ID3D11ShaderResourceView* Get(const std::string& path);
	std::vector<std::string> FindAll();
	std::string DisplayName(const std::string& path);
	bool IsBuiltin(const std::string& path);
}
