#pragma once

// C# 에서 부르는 네이티브 함수 표 (ScriptCore/Interop/NativeApi.cs 의 NativeApiTable 과 같은 순서).
namespace ScriptBindings
{
	int TableSize();
	void Fill(void* table);          // TableSize() 바이트
	void BeginFrame();               // 프레임마다 fileID → GameObject 캐시 비우기
	void Update();                   // 지연 Destroy 처리
	void Reset();                    // Play 종료 등: 대기 중인 작업 비우기
	NOVA_API GameObject* FindObject(uint64 fileID);   // 씬 + 이번 프레임에 만든 오브젝트 (캐시)
	const char* ReturnString(const std::string& s);   // 관리 코드로 돌려줄 임시 문자열
}
