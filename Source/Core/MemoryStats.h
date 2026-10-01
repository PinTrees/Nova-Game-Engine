#pragma once
#include <string>
#include <vector>

// 메모리 사용량 (Profiler 의 Memory 모듈).
//  - 프로세스: 개인 바이트(Private) / 작업 집합(Working Set) — GetProcessMemoryInfo
//  - GPU: 이 프로세스가 쓰는 전용 메모리와 예산 — IDXGIAdapter3::QueryVideoMemoryInfo
//  - 범주별: 엔진이 잡고 있는 자원(텍스처, 메시, 그림자 맵, 나무, 지형, Undo 기록 …)을 직접 센다
namespace MemoryStats
{
	struct Item
	{
		std::string Name;
		size_t Bytes = 0;
	};
	struct Category
	{
		const char* Name = "";    // 문자열 상수 (Profiler 통계 이름으로도 쓴다)
		bool Gpu = false;         // GPU 메모리 (아니면 CPU)
		size_t Bytes = 0;
		std::vector<Item> Items;  // 큰 것부터
	};
	struct Report
	{
		double TimeSec = 0.0;
		size_t ProcessPrivate = 0, WorkingSet = 0;
		size_t GpuUsage = 0, GpuBudget = 0;   // 0 = 알 수 없음
		std::vector<Category> Categories;
	};

	// D3D 자원 크기 (텍스처 = 밉·배열 포함, 버퍼 = ByteWidth)
	size_t ResourceBytes(GfxResource* resource);
	size_t ViewBytes(GfxView* view);

	void QueryProcess(Report& report);   // ProcessPrivate / WorkingSet / GpuUsage / GpuBudget

	std::string FormatBytes(size_t bytes);   // "12.3 MB"
}
