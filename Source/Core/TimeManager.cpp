#include "pch.h"
#include "TimeManager.h"

SINGLE_BODY(TimeManager)

TimeManager::TimeManager()
	: m_CurCount{}
	, m_Frequency{}
	, m_PrevCount{}
	, m_dDT(0.0f)
	, m_iCallCount(0)
	, m_dAcc(0)
	, mbStoped(false)
{

}

TimeManager::~TimeManager()
{

}

void TimeManager::Init()
{
	QueryPerformanceCounter(&m_PrevCount);
	QueryPerformanceFrequency(&m_Frequency);
}

void TimeManager::Update()
{
	QueryPerformanceCounter(&m_CurCount);
	// 이전 프레임의 카운팅과, 현재 프레임 카운팅 값의 차이를 구한다.
	m_dDT = (double)(m_CurCount.QuadPart - m_PrevCount.QuadPart) / (double)m_Frequency.QuadPart;

	// 이전카운트 값을 현재값으로 갱신 (다음번에 계산을 위해서)
	m_PrevCount = m_CurCount;
	m_iFPS = 1.f / m_dDT;

	//#ifdef _DEBUG
	//	//if (m_dDT > (1. / 60.))
	//	//	m_dDT = (1. / 60.);
	//#endif // DEBUG
}
