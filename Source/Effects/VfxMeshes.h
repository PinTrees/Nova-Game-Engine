#pragma once
#include <memory>
#include <string>
#include <vector>

// Visual Effect 의 메시 (Output Mesh · Collide with SDF): 엔진 기본 (Cube · Sphere · Cylinder · Cone · Crystal) 또는 모델 파일
//  (Assets/…/x.fbx — GLB · glTF · VRM 도, 뒤에 #2 면 그 파일의 세 번째 메시). CPU 사본 (자리 · 법선 · 인덱스) 을 이름마다 한 번 만든다
namespace Vfx
{
	struct CpuMesh
	{
		std::vector<float> PosNormal;      // 정점마다 자리 3 + 법선 3
		std::vector<uint32_t> Indices;     // 삼각형 목록
		float Min[3] = { 0, 0, 0 }, Max[3] = { 0, 0, 0 };
		std::string Error;                 // 비면 성공
		size_t VertexCount() const { return PosNormal.size() / 6; }
	};
	std::shared_ptr<const CpuMesh> LoadCpuMesh(const std::string& name);
	bool IsBuiltinMesh(const std::string& name);

	// Signed Distance Field (Unity 의 SDF Bake Tool 결과와 같은 쓰임): 메시 둘레 상자를 정육면체 칸으로 나눠 칸 가운데에서 표면까지 거리
	//  (안 = 음수). 가장 긴 변이 Resolution 칸. 메시마다 · 해상도마다 한 번 굽는다 (닫히지 않은 메시는 구멍으로 바깥이 샌다)
	struct Sdf
	{
		int N[3] = { 0, 0, 0 };
		float Min[3] = { 0, 0, 0 };        // 첫 칸의 모서리 (메시 좌표)
		float Voxel = 1.0f;                // 칸 한 변
		std::vector<float> Distance;       // x 가 가장 빠르게 (N[0] * N[1] * N[2])
		std::string Error;
		double BakeMs = 0.0;
	};
	std::shared_ptr<const Sdf> BakeSdf(const std::string& meshName, int resolution);
}
