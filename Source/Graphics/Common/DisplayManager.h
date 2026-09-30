#pragma once

class Camera;

class DisplayManager
{
	SINGLE_HEADER(DisplayManager)

private:
	vector<weak_ptr<Camera>> m_AllCameraComponents;  

	// Setting
	float				m_MainAspectRatio;
	map<string, float>	m_DefaultAspectRatios;

public:
	void Init();
	void RegisterCameraComponent(const weak_ptr<Camera>& camera); 
	void DeleteCameraComponent(const weak_ptr<Camera>& camera); 

	shared_ptr<Camera> GetActiveCamera();

	// Game 뷰의 Display 드롭다운: 이 디스플레이(0 = Display 1)를 Target Display 로 가진 카메라 중
	// 켜져 있고 Priority 가 가장 높은 것. 없으면 nullptr (= "No cameras rendering")
	shared_ptr<Camera> GetCameraForDisplay(int display);
	void SetActiveDisplay(int display) { m_ActiveDisplay = display; }
	int GetActiveDisplay() const { return m_ActiveDisplay; }

private:
	int m_ActiveDisplay = 0;
};

